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

bool language_id_is_cpp_family(const std::string& language_id);
bool language_id_is_python(const std::string& language_id);
bool language_id_is_shellscript(const std::string& language_id);
bool language_id_is_latex(const std::string& language_id);

std::string language_server_id_for_language(const std::string& language_id);

std::optional<LanguageServerSpec> make_clangd_spec(const std::string& workspace_root,
                                                   const std::string& compile_commands_dir,
                                                   bool use_gcc_query_driver,
                                                   bool background_index);

std::optional<LanguageServerSpec> make_basedpyright_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_bash_ls_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_texlab_spec(const std::string& workspace_root);

std::optional<LanguageServerSpec> make_language_server_spec(
    const std::string& server_id, const std::string& workspace_root,
    const std::string& compile_commands_dir = {}, bool use_gcc_query_driver = true,
    bool background_index = false);

}  // namespace tgdb
