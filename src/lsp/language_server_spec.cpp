#include "lsp/language_server_spec.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "util/bundled_tools.hpp"

namespace fs = std::filesystem;

namespace tuide {

bool language_id_is_cpp_family(const std::string& language_id) {
  return language_id == "c" || language_id == "cpp";
}

bool language_id_is_python(const std::string& language_id) {
  return language_id == "python";
}

bool language_id_is_shellscript(const std::string& language_id) {
  return language_id == "shellscript";
}

bool language_id_is_latex(const std::string& language_id) {
  return language_id == "latex";
}

bool language_id_is_rust(const std::string& language_id) {
  return language_id == "rust";
}

bool language_id_is_go(const std::string& language_id) {
  return language_id == "go";
}

bool language_id_is_zig(const std::string& language_id) {
  return language_id == "zig";
}

bool language_id_is_fortran(const std::string& language_id) {
  return language_id == "fortran";
}

bool language_id_is_lua(const std::string& language_id) {
  return language_id == "lua";
}

bool language_id_is_javascript(const std::string& language_id) {
  return language_id == "javascript";
}

bool language_id_is_typescript(const std::string& language_id) {
  return language_id == "typescript";
}

bool language_id_is_js_ts(const std::string& language_id) {
  return language_id_is_javascript(language_id) || language_id_is_typescript(language_id);
}

bool language_id_is_cmake(const std::string& language_id) {
  return language_id == "cmake";
}

bool language_id_is_make(const std::string& language_id) {
  return language_id == "make";
}

bool language_id_is_yaml(const std::string& language_id) {
  return language_id == "yaml";
}

bool language_id_is_xml(const std::string& language_id) {
  return language_id == "xml";
}

std::string language_server_id_for_language(const std::string& language_id) {
  if (language_id_is_python(language_id)) {
    return kLspServerBasedpyright;
  }
  if (language_id_is_shellscript(language_id)) {
    return kLspServerBash;
  }
  if (language_id_is_latex(language_id)) {
    return kLspServerTexlab;
  }
  if (language_id_is_rust(language_id)) {
    return kLspServerRustAnalyzer;
  }
  if (language_id_is_go(language_id)) {
    return kLspServerGopls;
  }
  if (language_id_is_zig(language_id)) {
    return kLspServerZls;
  }
  if (language_id_is_fortran(language_id)) {
    return kLspServerFortls;
  }
  if (language_id_is_lua(language_id)) {
    return kLspServerLuaLs;
  }
  if (language_id_is_js_ts(language_id)) {
    return kLspServerTypescriptLs;
  }
  if (language_id_is_cmake(language_id)) {
    return kLspServerNeocmakelsp;
  }
  if (language_id_is_make(language_id)) {
    return kLspServerMakeLs;
  }
  if (language_id_is_yaml(language_id)) {
    return kLspServerYamlLs;
  }
  if (language_id_is_xml(language_id)) {
    return kLspServerLemminx;
  }
  if (language_id_is_cpp_family(language_id)) {
    return kLspServerClangd;
  }
  return {};
}

namespace {

constexpr const char* kClangdQueryDriver =
    "/usr/bin/gcc*,/usr/bin/g++,/usr/bin/c++*,/usr/bin/clang*,"
    "/usr/local/bin/gcc*,/usr/local/bin/g++,/usr/local/bin/c++*,/usr/local/bin/clang*";

}  // namespace

std::optional<LanguageServerSpec> make_clangd_spec(const std::string& workspace_root,
                                                   const std::string& compile_commands_dir,
                                                   const bool use_gcc_query_driver,
                                                   const bool background_index) {
  const auto location = resolve_clangd();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerClangd;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"c", "cpp"};

  if (!location->resource_dir.empty()) {
    spec.args.push_back("--resource-dir=" + location->resource_dir);
  }
  if (!compile_commands_dir.empty()) {
    spec.args.push_back("--compile-commands-dir=" + compile_commands_dir);
  }
  if (use_gcc_query_driver) {
    spec.args.emplace_back(std::string("--query-driver=") + kClangdQueryDriver);
  }
  spec.args.emplace_back("-j=2");
  if (background_index) {
    spec.args.emplace_back("--background-index=true");
    spec.args.emplace_back("--background-index-priority=idle");
  } else {
    spec.args.emplace_back("--background-index=false");
  }
  return spec;
}

std::optional<LanguageServerSpec> make_basedpyright_spec(const std::string& workspace_root) {
  const auto location = resolve_basedpyright();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerBasedpyright;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"python"};
  if (location->use_python_module) {
    spec.args.emplace_back("-m");
    spec.args.push_back(location->python_module.empty() ? "basedpyright.langserver"
                                                        : location->python_module);
  }
  if (location->needs_stdio_flag) {
    spec.args.emplace_back("--stdio");
  }
  return spec;
}

std::optional<LanguageServerSpec> make_bash_ls_spec(const std::string& workspace_root) {
  const auto location = resolve_bash_language_server();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerBash;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"shellscript"};
  spec.args.emplace_back("start");
  spec.env.emplace_back("ENABLE_SOURCE_ERROR_DIAGNOSTICS=1");
  if (const auto shellcheck = resolve_shellcheck(); shellcheck.has_value()) {
    spec.env.emplace_back("SHELLCHECK_PATH=" + *shellcheck);
  }
  return spec;
}

std::optional<LanguageServerSpec> make_texlab_spec(const std::string& workspace_root) {
  const auto location = resolve_texlab();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerTexlab;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"latex", "plaintex", "bibtex"};
  // TexLab invokes `chktex` from PATH; ensure CHKTEX_PATH (or a non-PATH resolve) is visible.
  if (const auto chktex = resolve_chktex(); chktex.has_value()) {
    const fs::path chktex_dir = fs::path(*chktex).parent_path();
    if (!chktex_dir.empty()) {
      std::string path_env = "PATH=" + chktex_dir.string();
      if (const char* old_path = std::getenv("PATH"); old_path != nullptr && old_path[0] != '\0') {
        path_env.push_back(':');
        path_env += old_path;
      }
      spec.env.emplace_back(std::move(path_env));
    }
  }
  return spec;
}

std::string discover_project_root_with_marker(const std::string& start_path,
                                              const char* marker_filename) {
  if (start_path.empty() || marker_filename == nullptr || marker_filename[0] == '\0') {
    return {};
  }
  std::error_code ec;
  fs::path current = fs::absolute(start_path, ec);
  if (ec) {
    current = fs::path(start_path);
  }
  if (fs::is_regular_file(current, ec)) {
    current = current.parent_path();
  }
  while (!current.empty()) {
    if (fs::exists(current / marker_filename, ec) && !ec) {
      return current.string();
    }
    const fs::path parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return {};
}

std::optional<LanguageServerSpec> make_rust_analyzer_spec(const std::string& workspace_root) {
  const auto location = resolve_rust_analyzer();
  if (!location.has_value()) {
    return std::nullopt;
  }

  // rust-analyzer requires a Cargo project; prefer nearest Cargo.toml over the editor workspace.
  std::string cargo_root = discover_project_root_with_marker(workspace_root, "Cargo.toml");
  if (cargo_root.empty()) {
    cargo_root = workspace_root;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerRustAnalyzer;
  spec.command = location->binary_path;
  spec.workspace_root = cargo_root;
  spec.language_ids = {"rust"};
  return spec;
}

std::optional<LanguageServerSpec> make_gopls_spec(const std::string& workspace_root) {
  const auto location = resolve_gopls();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerGopls;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"go"};
  return spec;
}

std::optional<LanguageServerSpec> make_zls_spec(const std::string& workspace_root) {
  const auto location = resolve_zls();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerZls;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"zig"};
  return spec;
}

std::optional<LanguageServerSpec> make_fortls_spec(const std::string& workspace_root) {
  const auto location = resolve_fortls();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerFortls;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"fortran"};
  if (location->use_python_module) {
    spec.args.emplace_back("-m");
    spec.args.push_back(location->python_module.empty() ? "fortls" : location->python_module);
  }
  spec.args.emplace_back("--enable_code_actions");
  // Avoid fortls trying to pip-install updates into the (possibly bundled) env.
  spec.args.emplace_back("--disable_autoupdate");
  return spec;
}

std::optional<LanguageServerSpec> make_lua_ls_spec(const std::string& workspace_root) {
  const auto location = resolve_lua_language_server();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerLuaLs;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"lua"};
  return spec;
}

std::optional<LanguageServerSpec> make_typescript_ls_spec(const std::string& workspace_root) {
  const auto location = resolve_typescript_language_server();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerTypescriptLs;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"javascript", "typescript"};
  if (location->use_node_script) {
    spec.args.push_back(location->script_path);
    spec.args.emplace_back("--stdio");
  } else if (location->needs_stdio_flag) {
    spec.args.emplace_back("--stdio");
  }
  return spec;
}

std::optional<LanguageServerSpec> make_neocmakelsp_spec(const std::string& workspace_root) {
  const auto location = resolve_neocmakelsp();
  if (!location.has_value()) {
    return std::nullopt;
  }

  // neocmakelsp only emits useful diagnostics when a lint config exists (project
  // `.neocmake.toml` or user config). Seed a user default once so lint.enable
  // actually surfaces findings without requiring every workspace to opt in.
  {
    fs::path config_dir;
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') {
      config_dir = fs::path(xdg) / "neocmakelsp";
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      config_dir = fs::path(home) / ".config" / "neocmakelsp";
    }
    if (!config_dir.empty()) {
      const fs::path config_path = config_dir / "config.toml";
      std::error_code ec;
      if (!fs::exists(config_path, ec)) {
        fs::create_directories(config_dir, ec);
        if (!ec) {
          std::ofstream out(config_path);
          if (out) {
            out << "command_case = \"lower_case\"\n";
            out << "line_max_words = 120\n";
          }
        }
      }
    }
  }

  LanguageServerSpec spec;
  spec.id = kLspServerNeocmakelsp;
  spec.command = location->binary_path;
  spec.args = {"stdio"};
  spec.workspace_root = workspace_root;
  spec.language_ids = {"cmake"};
  // Lint is off unless enabled here; semantic_token off — Tree-sitter owns highlighting.
  spec.initialization_options_json =
      R"({"lint":{"enable":true},"format":{"enable":true},"semantic_token":false})";
  return spec;
}

std::optional<LanguageServerSpec> make_make_ls_spec(const std::string& workspace_root) {
  const auto location = resolve_make_ls();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerMakeLs;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"make"};
  return spec;
}

std::optional<LanguageServerSpec> make_yaml_ls_spec(const std::string& workspace_root) {
  const auto location = resolve_yaml_language_server();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerYamlLs;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"yaml"};
  if (location->use_node_script) {
    spec.args.push_back(location->script_path);
    spec.args.emplace_back("--stdio");
  } else if (location->needs_stdio_flag) {
    spec.args.emplace_back("--stdio");
  }
  return spec;
}

std::optional<LanguageServerSpec> make_lemminx_spec(const std::string& workspace_root) {
  const auto location = resolve_lemminx();
  if (!location.has_value()) {
    return std::nullopt;
  }

  LanguageServerSpec spec;
  spec.id = kLspServerLemminx;
  spec.command = location->binary_path;
  spec.workspace_root = workspace_root;
  spec.language_ids = {"xml"};
  return spec;
}

std::optional<LanguageServerSpec> make_language_server_spec(
    const std::string& server_id, const std::string& workspace_root,
    const std::string& compile_commands_dir, const bool use_gcc_query_driver,
    const bool background_index) {
  if (server_id == kLspServerClangd) {
    return make_clangd_spec(workspace_root, compile_commands_dir, use_gcc_query_driver,
                            background_index);
  }
  if (server_id == kLspServerBasedpyright) {
    return make_basedpyright_spec(workspace_root);
  }
  if (server_id == kLspServerBash) {
    return make_bash_ls_spec(workspace_root);
  }
  if (server_id == kLspServerTexlab) {
    return make_texlab_spec(workspace_root);
  }
  if (server_id == kLspServerRustAnalyzer) {
    return make_rust_analyzer_spec(workspace_root);
  }
  if (server_id == kLspServerGopls) {
    return make_gopls_spec(workspace_root);
  }
  if (server_id == kLspServerZls) {
    return make_zls_spec(workspace_root);
  }
  if (server_id == kLspServerFortls) {
    return make_fortls_spec(workspace_root);
  }
  if (server_id == kLspServerLuaLs) {
    return make_lua_ls_spec(workspace_root);
  }
  if (server_id == kLspServerTypescriptLs) {
    return make_typescript_ls_spec(workspace_root);
  }
  if (server_id == kLspServerNeocmakelsp) {
    return make_neocmakelsp_spec(workspace_root);
  }
  if (server_id == kLspServerMakeLs) {
    return make_make_ls_spec(workspace_root);
  }
  if (server_id == kLspServerYamlLs) {
    return make_yaml_ls_spec(workspace_root);
  }
  if (server_id == kLspServerLemminx) {
    return make_lemminx_spec(workspace_root);
  }
  return std::nullopt;
}

}  // namespace tuide
