#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tgdb {

// Descriptor for a language server that can be offered via the "LSP missing" toast.
struct LspMissingPromptInfo {
  std::string server_id;           // e.g. "gopls"
  std::string language_i18n_key;   // e.g. "lsp_toast.lang.go"
  std::string status_missing_key;  // e.g. "status.gopls_missing"
  std::string install_command;     // typed into the terminal (no trailing newline)
  std::string bundle_cli_flag;     // e.g. "--bundle-gopls" (empty if not bundleable)
  // Key written into .bundle-config (e.g. "BUNDLE_GOPLS"). Empty if not bundleable.
  std::string bundle_config_key;
  // Value for bundle_config_key, or for PYTHON_BUNDLE_KIND ("lsp_min").
  std::string bundle_config_value = "1";
};

std::optional<LspMissingPromptInfo> lsp_missing_prompt_for_status_key(const std::string& i18n_key);

bool is_lsp_missing_status_key(const std::string& i18n_key);

// True when root looks like a checkout of this project (can rebuild with bundles).
bool looks_like_tgdb_source_root(const std::string& path);

// Walk candidates (and parents) for a tgdb source tree. Also probes /proc/self/exe.
std::optional<std::string> find_tgdb_source_root(const std::vector<std::string>& search_roots);

// Merge-enable the bundle option for this server into ${tgdb_root}/.bundle-config.
bool enable_bundle_option_in_config(const std::string& tgdb_root, const LspMissingPromptInfo& info);

// ./tools/compile.sh -y  (config already updated) — relative path for display in shell.
std::string compile_command_after_bundle_config();

struct PostExitShellRequest {
  std::string cwd;
  std::string command;  // prefilled; user presses Enter
};

void request_post_exit_shell(PostExitShellRequest request);
std::optional<PostExitShellRequest> consume_post_exit_shell_request();

// bash -c script that cds, prefills command with read -e -i, runs on Enter.
std::string make_post_exit_bash_script(const PostExitShellRequest& request);

}  // namespace tgdb
