#include "lsp/language_server_spec.hpp"

#include <cstdlib>
#include <filesystem>

#include "util/bundled_tools.hpp"

namespace fs = std::filesystem;

namespace tgdb {

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
  return std::nullopt;
}

}  // namespace tgdb
