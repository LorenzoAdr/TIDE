#include "build/compile_commands_generator.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <nlohmann/json.hpp>

#include "util/compile_commands_setup.hpp"
#include "util/shell_utils.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

constexpr int kGenerationTimeoutSeconds = 120;

std::string join_command(const std::vector<std::string>& parts) {
  std::ostringstream out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out << ' ';
    }
    out << parts[i];
  }
  return out.str();
}

std::string env_prefix(const BuildEnvironment& environment) {
  std::ostringstream out;
  for (const auto& entry : environment.env_vars) {
    out << entry.first << '=' << shell_quote(entry.second) << ' ';
  }
  return out.str();
}

std::string setup_prefix(const BuildEnvironment& environment) {
  if (environment.setup_scripts.empty()) {
    return {};
  }
  std::ostringstream out;
  for (const auto& script : environment.setup_scripts) {
    out << "set -a && source " << shell_quote(script) << " >/dev/null 2>&1 && set +a && ";
  }
  return out.str();
}

std::string make_target_suffix(const WorkspaceConfig& config) {
  if (config.build_environments.make_default_target.empty()) {
    return {};
  }
  return " " + config.build_environments.make_default_target;
}

bool write_text_file(const fs::path& path, const std::string& text) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << text;
  if (!text.empty() && text.back() != '\n') {
    output << '\n';
  }
  return static_cast<bool>(output);
}

bool write_executable(const fs::path& path, const std::string& content) {
  if (!write_text_file(path, content)) {
    return false;
  }
  std::error_code ec;
  fs::permissions(path, fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write,
                  fs::perm_options::add, ec);
  return !ec;
}

bool run_shell_command(const std::string& command) {
  const int status = std::system(command.c_str());
  return status == 0;
}

bool run_shell_command_in_dir(const std::string& working_dir, const std::string& command) {
  const std::string wrapped = "cd " + shell_quote(working_dir) + " && " + command;
  return run_shell_command(wrapped);
}

bool is_compiler_invocation(const std::string& line) {
  static const std::regex kPattern(
      R"((^|\s)(gcc|g\+\+|clang|clang\+\+|cc|c\+\+)(?=\s|$))");
  return std::regex_search(line, kPattern);
}

std::string extract_source_from_compiler_line(const std::string& line) {
  std::istringstream stream(line);
  std::string token;
  std::string candidate;
  while (stream >> token) {
    if (token.size() > 2 && (token.rfind("-I", 0) == 0 || token.rfind("-D", 0) == 0 ||
                             token.rfind("-l", 0) == 0 || token.rfind("-L", 0) == 0 ||
                             token == "-c" || token == "-o")) {
      continue;
    }
    if (token.size() > 2 && token[0] != '-' && (token.find(".c") != std::string::npos ||
                                                  token.find(".cc") != std::string::npos ||
                                                  token.find(".cpp") != std::string::npos ||
                                                  token.find(".cxx") != std::string::npos)) {
      candidate = token;
    }
  }
  return candidate;
}

nlohmann::json compile_commands_from_dry_run(const std::string& working_dir,
                                             const std::string& make_command) {
  nlohmann::json entries = nlohmann::json::array();
  const std::string command = "cd " + shell_quote(working_dir) + " && " + make_command +
                              " -n 2>/dev/null";
  const std::string output = run_shell_capture(command, kGenerationTimeoutSeconds);
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!is_compiler_invocation(line)) {
      continue;
    }
    const std::string file = extract_source_from_compiler_line(line);
    if (file.empty()) {
      continue;
    }
    entries.push_back({
        {"directory", working_dir},
        {"command", line},
        {"file", file},
    });
  }
  return entries;
}

bool write_compile_commands_json(const fs::path& output_path, const nlohmann::json& entries) {
  if (!entries.is_array() || entries.empty()) {
    return false;
  }
  return write_text_file(output_path, entries.dump(2));
}

bool intercept_log_to_compile_commands(const fs::path& log_path, const std::string& working_dir,
                                       const fs::path& output_path) {
  std::ifstream input(log_path);
  if (!input) {
    return false;
  }
  nlohmann::json entries = nlohmann::json::array();
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    try {
      const auto doc = nlohmann::json::parse(line);
      if (!doc.is_object() || !doc.contains("file") || !doc["file"].is_string()) {
        continue;
      }
      nlohmann::json entry = doc;
      if (!entry.contains("directory")) {
        entry["directory"] = working_dir;
      }
      entries.push_back(std::move(entry));
    } catch (...) {
    }
  }
  return write_compile_commands_json(output_path, entries);
}

bool install_builtin_wrappers(const fs::path& bin_dir, const fs::path& log_path,
                              const BuildEnvironment& environment) {
  std::error_code ec;
  fs::create_directories(bin_dir, ec);
  const std::string real_cc = environment.env_vars.count("CC") ? environment.env_vars.at("CC")
                                                               : "gcc";
  const std::string real_cxx = environment.env_vars.count("CXX") ? environment.env_vars.at("CXX")
                                                                 : "g++";
  const std::string wrapper_template = R"WRAPPER(#!/bin/sh
REAL="$REAL_COMPILER"
LOG="$INTERCEPT_LOG"
if [ -n "$LOG" ]; then
  escaped=$(printf '%s' "$@" | sed 's/\\/\\\\/g; s/"/\\"/g')
  file=""
  for arg in "$@"; do
    case "$arg" in
      *.c|*.cc|*.cpp|*.cxx) file="$arg" ;;
    esac
  done
  printf '{"directory":"%s","command":"%s %s","file":"%s"}\n' "$(pwd)" "$(basename "$0")" "$escaped" "$file" >> "$LOG"
fi
exec "$REAL" "$@"
)WRAPPER";
  auto make_wrapper = [&](const fs::path& path, const std::string& real_compiler) {
    std::string script = wrapper_template;
    const auto pos = script.find("$REAL_COMPILER");
    script.replace(pos, 14, real_compiler);
    const auto log_pos = script.find("$INTERCEPT_LOG");
    script.replace(log_pos, 14, log_path.string());
    return write_executable(path, script);
  };
  return make_wrapper(bin_dir / "cc", real_cc) && make_wrapper(bin_dir / "c++", real_cxx);
}

MakeInterceptTool resolve_intercept_tool(const WorkspaceConfig& config) {
  if (config.build_environments.make_intercept_tool != MakeInterceptTool::kAuto) {
    return config.build_environments.make_intercept_tool;
  }
  if (command_exists("bear")) {
    return MakeInterceptTool::kBear;
  }
  if (command_exists("compiledb")) {
    return MakeInterceptTool::kCompiledb;
  }
  return MakeInterceptTool::kBuiltin;
}

bool generate_with_bear(const std::string& workspace_root, const BuildEnvironment& environment,
                        const WorkspaceConfig& config, const fs::path& output_path) {
  if (!command_exists("bear")) {
    return false;
  }
  const std::string output = output_path.string();
  const std::string make_cmd = environment.make_command + make_target_suffix(config);
  std::string command = env_prefix(environment) + setup_prefix(environment) + "bear --output " +
                        shell_quote(output) + " -- " + make_cmd;
  if (!environment.docker_container.empty()) {
    command = "docker exec -i -w " + shell_quote(environment.working_dir) + ' ' +
              shell_quote(environment.docker_container) + " /bin/bash -lc " +
              shell_quote(command);
    return run_shell_command(command);
  }
  return run_shell_command_in_dir(environment.working_dir, command);
}

bool generate_with_compiledb(const BuildEnvironment& environment, const WorkspaceConfig& config,
                             const fs::path& output_path) {
  if (!command_exists("compiledb")) {
    return false;
  }
  const std::string make_cmd = environment.make_command + make_target_suffix(config);
  const std::string command = env_prefix(environment) + setup_prefix(environment) +
                              "compiledb -o " + shell_quote(output_path.string()) + " -n " +
                              make_cmd;
  return run_shell_command_in_dir(environment.working_dir, command);
}

bool generate_with_builtin(const BuildEnvironment& environment, const WorkspaceConfig& config,
                           const fs::path& output_dir, const fs::path& output_path) {
  const fs::path bin_dir = fs::path(output_dir) / "bin";
  const fs::path log_path = fs::path(output_dir) / "intercept.log";
  std::error_code ec;
  fs::remove(log_path, ec);
  if (!install_builtin_wrappers(bin_dir, log_path, environment)) {
    return false;
  }
  const std::string make_cmd = environment.make_command + make_target_suffix(config);
  std::ostringstream command;
  command << env_prefix(environment) << setup_prefix(environment) << "CC=" << bin_dir / "cc"
          << " CXX=" << bin_dir / "c++" << ' ' << make_cmd;
  if (!run_shell_command_in_dir(environment.working_dir, command.str())) {
    return false;
  }
  return intercept_log_to_compile_commands(log_path, environment.working_dir, output_path);
}

bool generate_with_docker_bear(const BuildEnvironment& environment, const WorkspaceConfig& config,
                               const fs::path& output_path) {
  if (environment.docker_container.empty() || !command_exists("docker") ||
      !command_exists("bear")) {
    return false;
  }
  const std::string container_output = "/tmp/tuide_compile_commands.json";
  const std::string make_cmd = environment.make_command + make_target_suffix(config);
  const std::string remote_command = "bear --output " + shell_quote(container_output) + " -- " +
                                     make_cmd;
  const std::string exec_command = "docker exec -i -w " + shell_quote(environment.working_dir) +
                                   ' ' + shell_quote(environment.docker_container) +
                                   " /bin/bash -lc " + shell_quote(remote_command);
  if (!run_shell_command(exec_command)) {
    return false;
  }
  const std::string fetch_command = "docker exec " + shell_quote(environment.docker_container) +
                                    " cat " + shell_quote(container_output) + " 2>/dev/null";
  const std::string json_text = run_shell_capture(fetch_command, 10);
  if (json_text.empty()) {
    return false;
  }
  try {
    const auto doc = nlohmann::json::parse(json_text);
    if (!doc.is_array() || doc.empty()) {
      return false;
    }
  } catch (...) {
    return false;
  }
  return write_text_file(output_path, json_text);
}

}  // namespace

bool compile_commands_exists(const std::string& compile_dir) {
  if (compile_dir.empty()) {
    return false;
  }
  std::error_code ec;
  return fs::is_regular_file(fs::path(compile_dir) / "compile_commands.json", ec);
}

CompileCommandsGenerationResult generate_compile_commands(
    const std::string& workspace_root, const BuildEnvironment& environment,
    const WorkspaceConfig& config) {
  CompileCommandsGenerationResult result;
  if (workspace_root.empty() || environment.working_dir.empty()) {
    return result;
  }

  const fs::path output_dir = environment_compile_dir(workspace_root, environment.id);
  const fs::path output_path = output_dir / "compile_commands.json";
  result.compile_dir = output_dir.string();
  result.fallback_compile_flags = environment.fallback_compile_flags;

  if (compile_commands_exists(result.compile_dir)) {
    result.success = true;
    result.method = "cache";
    return result;
  }

  const std::string existing = find_compile_commands_dir(workspace_root);
  if (!existing.empty()) {
    std::error_code ec;
    fs::create_directories(output_dir, ec);
    fs::copy_file(fs::path(existing) / "compile_commands.json", output_path,
                  fs::copy_options::overwrite_existing, ec);
    if (!ec) {
      result.success = true;
      result.method = "existing";
      return result;
    }
  }

  const MakeInterceptTool tool = resolve_intercept_tool(config);
  if (tool == MakeInterceptTool::kBear || tool == MakeInterceptTool::kAuto) {
    if (!environment.docker_container.empty()) {
      if (generate_with_docker_bear(environment, config, output_path)) {
        result.success = true;
        result.method = "docker_bear";
        return result;
      }
    } else if (generate_with_bear(workspace_root, environment, config, output_path)) {
      result.success = true;
      result.method = "bear";
      return result;
    }
  }

  if (tool == MakeInterceptTool::kCompiledb || tool == MakeInterceptTool::kAuto) {
    if (generate_with_compiledb(environment, config, output_path)) {
      result.success = true;
      result.method = "compiledb";
      return result;
    }
  }

  if (tool == MakeInterceptTool::kBuiltin || tool == MakeInterceptTool::kAuto) {
    if (generate_with_builtin(environment, config, output_dir, output_path)) {
      result.success = true;
      result.method = "builtin";
      return result;
    }
  }

  const nlohmann::json dry_run_entries =
      compile_commands_from_dry_run(environment.working_dir, environment.make_command);
  if (write_compile_commands_json(output_path, dry_run_entries)) {
    result.success = true;
    result.method = "make_dry_run";
    return result;
  }

  result.fallback_compile_flags = environment.fallback_compile_flags;
  return result;
}

}  // namespace tuide
