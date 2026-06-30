#include "util/compile_commands_lookup.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "util/clangd_workspace_setup.hpp"
#include "util/compile_commands_remap.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string canonical_path(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(fs::path(path), ec);
  return ec ? normalize_path(path) : normalize_path(canonical.string());
}

bool paths_equal(const std::string& a, const std::string& b) {
  if (a.empty() || b.empty()) {
    return false;
  }
  return canonical_path(a) == canonical_path(b);
}

std::string resolve_entry_file_path(const std::string& file_field,
                                    const std::string& directory_field) {
  if (file_field.empty()) {
    return {};
  }
  const fs::path file_path(file_field);
  if (file_path.is_absolute()) {
    return canonical_path(file_field);
  }
  if (!directory_field.empty()) {
    return canonical_path((fs::path(directory_field) / file_path).string());
  }
  return canonical_path(file_field);
}

void push_unique(std::vector<std::string>* out, const std::string& value) {
  if (out == nullptr || value.empty()) {
    return;
  }
  if (std::find(out->begin(), out->end(), value) == out->end()) {
    out->push_back(value);
  }
}

void collect_flag_value(const std::string& arg, const std::string& prefix,
                        std::vector<std::string>* out) {
  if (arg.size() <= prefix.size() || arg.rfind(prefix, 0) != 0) {
    return;
  }
  const std::string value = arg.substr(prefix.size());
  if (!value.empty()) {
    push_unique(out, value);
  }
}

void collect_flags_from_arguments(const std::vector<std::string>& args,
                                  std::vector<std::string>* includes,
                                  std::vector<std::string>* system_includes,
                                  std::vector<std::string>* defines) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "-I" || arg == "-iquote") {
      if (i + 1 < args.size()) {
        push_unique(includes, args[++i]);
      }
      continue;
    }
    if (arg == "-isystem" || arg == "-idirafter") {
      if (i + 1 < args.size()) {
        push_unique(system_includes, args[++i]);
      }
      continue;
    }
    if (arg == "-D") {
      if (i + 1 < args.size()) {
        push_unique(defines, args[++i]);
      }
      continue;
    }
    collect_flag_value(arg, "-I", includes);
    collect_flag_value(arg, "-iquote", includes);
    collect_flag_value(arg, "-isystem", system_includes);
    collect_flag_value(arg, "-idirafter", system_includes);
    collect_flag_value(arg, "-D", defines);
  }
}

std::vector<std::string> split_command_line(const std::string& command) {
  std::vector<std::string> tokens;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  bool escape = false;
  for (char ch : command) {
    if (escape) {
      current.push_back(ch);
      escape = false;
      continue;
    }
    if (ch == '\\' && in_double) {
      escape = true;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !in_single && !in_double) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

std::vector<std::string> read_clangd_extra_flags(const std::string& workspace_root) {
  std::vector<std::string> flags;
  if (workspace_root.empty()) {
    return flags;
  }
  std::ifstream input(fs::path(workspace_root) / ".clangd");
  if (!input) {
    return flags;
  }
  bool in_add = false;
  std::string line;
  while (std::getline(input, line)) {
    const auto start = line.find_first_not_of(" \t");
    if (start == std::string::npos) {
      continue;
    }
    const std::string trimmed = line.substr(start);
    if (trimmed == "Add:") {
      in_add = true;
      continue;
    }
    if (!in_add) {
      continue;
    }
    if (trimmed.rfind("- ", 0) == 0) {
      flags.push_back(trimmed.substr(2));
      continue;
    }
    if (!trimmed.empty() && trimmed.back() == ':') {
      in_add = false;
    }
  }
  return flags;
}

const nlohmann::json* find_compile_entry(const nlohmann::json& doc,
                                         const std::string& host_absolute_path) {
  if (!doc.is_array()) {
    return nullptr;
  }
  const std::string target = canonical_path(host_absolute_path);
  const nlohmann::json* fallback = nullptr;
  for (const auto& entry : doc) {
    if (!entry.is_object() || !entry.contains("file") || !entry["file"].is_string()) {
      continue;
    }
    const std::string directory =
        entry.contains("directory") && entry["directory"].is_string()
            ? entry["directory"].get<std::string>()
            : std::string{};
    const std::string entry_path =
        resolve_entry_file_path(entry["file"].get<std::string>(), directory);
    if (paths_equal(entry_path, target)) {
      return &entry;
    }
    if (fallback == nullptr && paths_equal(entry["file"].get<std::string>(), target)) {
      fallback = &entry;
    }
  }
  return fallback;
}

void fill_from_entry(const nlohmann::json& entry, FileIndexerPaths* result) {
  if (result == nullptr) {
    return;
  }
  result->entry_found = true;
  if (entry.contains("file") && entry["file"].is_string()) {
    result->entry_file = entry["file"].get<std::string>();
  }
  if (entry.contains("directory") && entry["directory"].is_string()) {
    result->entry_directory = entry["directory"].get<std::string>();
  }
  if (entry.contains("output") && entry["output"].is_string()) {
    result->entry_output = entry["output"].get<std::string>();
  }
  if (entry.contains("arguments") && entry["arguments"].is_array()) {
    for (const auto& arg : entry["arguments"]) {
      if (arg.is_string()) {
        result->compile_arguments.push_back(arg.get<std::string>());
      }
    }
  } else if (entry.contains("command") && entry["command"].is_string()) {
    result->compile_arguments = split_command_line(entry["command"].get<std::string>());
  }
  collect_flags_from_arguments(result->compile_arguments, &result->include_paths,
                               &result->system_include_paths, &result->defines);
}

void append_section(std::vector<std::string>* lines, const std::string& title) {
  if (lines == nullptr) {
    return;
  }
  if (!lines->empty()) {
    lines->push_back("");
  }
  lines->push_back(title);
}

void append_values(std::vector<std::string>* lines, const std::string& prefix,
                   const std::vector<std::string>& values) {
  if (lines == nullptr) {
    return;
  }
  if (values.empty()) {
    lines->push_back(prefix + "(ninguna)");
    return;
  }
  for (const std::string& value : values) {
    lines->push_back(prefix + value);
  }
}

}  // namespace

FileIndexerPaths lookup_file_indexer_paths(const std::string& workspace_root,
                                           const WorkspaceConfig& config,
                                           const std::string& host_absolute_path) {
  FileIndexerPaths result;
  result.clangd_extra_flags = read_clangd_extra_flags(workspace_root);
  if (!config.clangd_extra_include_paths.empty() && result.clangd_extra_flags.empty()) {
    const std::vector<std::string> expanded =
        expand_recursive_include_flags(config.clangd_extra_include_paths);
    result.clangd_extra_flags = expanded;
  }

  if (workspace_root.empty() || host_absolute_path.empty()) {
    result.display_lines.push_back("Rutas de indexación (clangd)");
    result.display_lines.push_back("(sin workspace o archivo)");
    return result;
  }

  const auto setup = ensure_compile_commands_for_clangd(workspace_root, config);
  result.compile_commands_dir = setup.compile_dir;
  result.status_note = setup.status_note;

  if (!setup.compile_dir.empty()) {
    const fs::path db_path = fs::path(setup.compile_dir) / "compile_commands.json";
    std::ifstream input(db_path);
    if (input) {
      try {
        nlohmann::json doc = nlohmann::json::parse(input);
        if (const nlohmann::json* entry = find_compile_entry(doc, host_absolute_path)) {
          fill_from_entry(*entry, &result);
        }
      } catch (...) {
      }
    }
  }

  result.display_lines.clear();
  result.display_lines.push_back("Rutas de indexación (clangd)");
  result.display_lines.push_back("Archivo host: " + canonical_path(host_absolute_path));
  if (!result.status_note.empty()) {
    result.display_lines.push_back("Nota: " + result.status_note);
  }
  if (result.compile_commands_dir.empty()) {
    result.display_lines.push_back("compile_commands_dir: (no configurado)");
  } else {
    result.display_lines.push_back("compile_commands_dir: " + result.compile_commands_dir);
  }

  if (!result.entry_found) {
    append_section(&result.display_lines, "Entrada compile_commands");
    result.display_lines.push_back("(sin entrada para este archivo)");
  } else {
    append_section(&result.display_lines, "Entrada compile_commands");
    result.display_lines.push_back("file: " + result.entry_file);
    if (!result.entry_directory.empty()) {
      result.display_lines.push_back("directory: " + result.entry_directory);
    }
    if (!result.entry_output.empty()) {
      result.display_lines.push_back("output: " + result.entry_output);
    }
    append_section(&result.display_lines, "Includes (-I, -iquote)");
    append_values(&result.display_lines, "  ", result.include_paths);
    append_section(&result.display_lines, "Includes sistema (-isystem, -idirafter)");
    append_values(&result.display_lines, "  ", result.system_include_paths);
    append_section(&result.display_lines, "Defines (-D)");
    append_values(&result.display_lines, "  ", result.defines);
    if (!result.compile_arguments.empty()) {
      append_section(&result.display_lines, "Argumentos de compilación");
      for (const std::string& arg : result.compile_arguments) {
        result.display_lines.push_back("  " + arg);
      }
    }
  }

  append_section(&result.display_lines, "Flags extra (.clangd / config)");
  append_values(&result.display_lines, "  ", result.clangd_extra_flags);

  return result;
}

}  // namespace tgdb
