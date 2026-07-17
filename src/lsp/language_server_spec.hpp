#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace tgdb {

// Descriptor of a stdio language server. clangd and basedpyright are specs, not
// forks of LspClient.
struct LanguageServerSpec {
  std::string id;  // "clangd", "basedpyright", ...
  std::string command;
  std::vector<std::string> args;
  std::unordered_set<std::string> language_ids;
  std::string workspace_root;
  // Optional env overrides applied in the child before exec (KEY=VALUE).
  std::vector<std::string> env;
};

inline constexpr const char* kLspServerClangd = "clangd";
inline constexpr const char* kLspServerBasedpyright = "basedpyright";
inline constexpr const char* kLspServerBash = "bash-language-server";
inline constexpr const char* kLspServerTexlab = "texlab";
inline constexpr const char* kLspServerRustAnalyzer = "rust-analyzer";
inline constexpr const char* kLspServerGopls = "gopls";
inline constexpr const char* kLspServerZls = "zls";
inline constexpr const char* kLspServerFortls = "fortls";
inline constexpr const char* kLspServerLuaLs = "lua-language-server";
inline constexpr const char* kLspServerTypescriptLs = "typescript-language-server";

bool language_id_is_cpp_family(const std::string& language_id);
bool language_id_is_python(const std::string& language_id);
bool language_id_is_shellscript(const std::string& language_id);
bool language_id_is_latex(const std::string& language_id);
bool language_id_is_rust(const std::string& language_id);
bool language_id_is_go(const std::string& language_id);
bool language_id_is_zig(const std::string& language_id);
bool language_id_is_fortran(const std::string& language_id);
bool language_id_is_lua(const std::string& language_id);
bool language_id_is_javascript(const std::string& language_id);
bool language_id_is_typescript(const std::string& language_id);
bool language_id_is_js_ts(const std::string& language_id);

std::string language_server_id_for_language(const std::string& language_id);

std::optional<LanguageServerSpec> make_clangd_spec(const std::string& workspace_root,
                                                   const std::string& compile_commands_dir,
                                                   bool use_gcc_query_driver,
                                                   bool background_index);

std::optional<LanguageServerSpec> make_basedpyright_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_bash_ls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_texlab_spec(const std::string& workspace_root);

// Walks up from path (file or directory) looking for Cargo.toml / go.mod / etc.
std::string discover_project_root_with_marker(const std::string& start_path,
                                              const char* marker_filename);

std::optional<LanguageServerSpec> make_rust_analyzer_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_gopls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_zls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_fortls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_lua_ls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_typescript_ls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_language_server_spec(
    const std::string& server_id, const std::string& workspace_root,
    const std::string& compile_commands_dir = {}, bool use_gcc_query_driver = true,
    bool background_index = false);

}  // namespace tgdb
