#include "util/lsp_missing_prompt.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace tgdb {
namespace {

namespace fs = std::filesystem;

std::mutex g_post_exit_mutex;
std::optional<PostExitShellRequest> g_post_exit_request;

bool command_on_path(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  const char* path_env = std::getenv("PATH");
  if (path_env == nullptr || path_env[0] == '\0') {
    return false;
  }
  std::string path = path_env;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t end = path.find(':', start);
    const std::string dir =
        path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!dir.empty()) {
      std::error_code ec;
      const fs::path candidate = fs::path(dir) / name;
      if (fs::is_regular_file(candidate, ec) &&
          (fs::status(candidate, ec).permissions() & fs::perms::owner_exec) != fs::perms::none) {
        return true;
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  return false;
}

std::string tgdb_tools_venv_pip_install(const char* package) {
  return std::string("python3 -m venv ~/.venvs/tgdb-tools && "
                     "~/.venvs/tgdb-tools/bin/pip install ") +
         package +
         " && export PATH=\"$HOME/.venvs/tgdb-tools/bin:$PATH\" && "
         "echo \"Reinicia tgdb o asegúrate de que ~/.venvs/tgdb-tools/bin está en PATH\"";
}

std::string prefer_apt(const char* apt_package, const std::string& fallback) {
  if (apt_package != nullptr && apt_package[0] != '\0' && command_on_path("apt-get")) {
    return std::string("sudo apt-get install -y ") + apt_package;
  }
  return fallback;
}

const std::vector<LspMissingPromptInfo>& catalog() {
  static const std::vector<LspMissingPromptInfo> kCatalog = [] {
    std::vector<LspMissingPromptInfo> entries;
    entries.push_back({
        "basedpyright",
        "lsp_toast.lang.python",
        "status.basedpyright_missing",
        tgdb_tools_venv_pip_install("basedpyright"),
        "--bundle-python-lsp-min",
        "PYTHON_BUNDLE_KIND",
        "lsp_min",
    });
    entries.push_back({
        "bash-language-server",
        "lsp_toast.lang.shell",
        "status.bash_ls_missing",
        prefer_apt(nullptr, "npm install -g bash-language-server"),
        "--bundle-bash-ls",
        "BUNDLE_BASH_LS",
        "1",
    });
    entries.push_back({
        "texlab",
        "lsp_toast.lang.latex",
        "status.texlab_missing",
        prefer_apt("texlab",
                   "echo \"Descarga texlab desde https://github.com/latex-lsp/texlab/releases "
                   "y colócalo en PATH\""),
        "--bundle-texlab",
        "BUNDLE_TEXLAB",
        "1",
    });
    entries.push_back({
        "rust-analyzer",
        "lsp_toast.lang.rust",
        "status.rust_analyzer_missing",
        prefer_apt(nullptr, "rustup component add rust-analyzer"),
        "--bundle-rust-analyzer",
        "BUNDLE_RUST_ANALYZER",
        "1",
    });
    entries.push_back({
        "gopls",
        "lsp_toast.lang.go",
        "status.gopls_missing",
        prefer_apt(nullptr, "go install golang.org/x/tools/gopls@latest"),
        "--bundle-gopls",
        "BUNDLE_GOPLS",
        "1",
    });
    entries.push_back({
        "zls",
        "lsp_toast.lang.zig",
        "status.zls_missing",
        prefer_apt(nullptr,
                   "echo \"Descarga zls desde https://github.com/zigtools/zls/releases "
                   "y colócalo en PATH\""),
        "--bundle-zls",
        "BUNDLE_ZLS",
        "1",
    });
    entries.push_back({
        "fortls",
        "lsp_toast.lang.fortran",
        "status.fortls_missing",
        tgdb_tools_venv_pip_install("fortls"),
        "--bundle-fortls",
        "BUNDLE_FORTLS",
        "1",
    });
    entries.push_back({
        "lua-language-server",
        "lsp_toast.lang.lua",
        "status.lua_ls_missing",
        prefer_apt("lua-language-server",
                   "echo \"Descarga lua-language-server desde "
                   "https://github.com/LuaLS/lua-language-server/releases y colócalo en PATH\""),
        "--bundle-lua-ls",
        "BUNDLE_LUA_LS",
        "1",
    });
    entries.push_back({
        "typescript-language-server",
        "lsp_toast.lang.typescript",
        "status.typescript_ls_missing",
        prefer_apt(nullptr, "npm install -g typescript typescript-language-server"),
        "--bundle-tsserver",
        "BUNDLE_TSSERVER",
        "1",
    });
    entries.push_back({
        "neocmakelsp",
        "lsp_toast.lang.cmake",
        "status.neocmakelsp_missing",
        prefer_apt(nullptr,
                   "echo \"Descarga neocmakelsp desde "
                   "https://github.com/neocmakelsp/neocmakelsp/releases y colócalo en PATH\""),
        "--bundle-neocmakelsp",
        "BUNDLE_NEOCMAKELSP",
        "1",
    });
    entries.push_back({
        "make-ls",
        "lsp_toast.lang.make",
        "status.make_ls_missing",
        prefer_apt(nullptr,
                   "echo \"Descarga make-ls desde https://github.com/owenrumney/make-ls/releases "
                   "y colócalo en PATH\""),
        "--bundle-make-ls",
        "BUNDLE_MAKE_LS",
        "1",
    });
    return entries;
  }();
  return kCatalog;
}

std::string shell_single_quote(const std::string& value) {
  std::string out = "'";
  for (char ch : value) {
    if (ch == '\'') {
      out += "'\\''";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('\'');
  return out;
}

using ConfigMap = std::unordered_map<std::string, std::string>;

ConfigMap load_bundle_config_map(const fs::path& path) {
  ConfigMap map;
  std::ifstream input(path);
  if (!input) {
    return map;
  }
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    map[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return map;
}

void write_bundle_config_map(const fs::path& path, const ConfigMap& map) {
  auto get = [&](const char* key, const char* fallback) -> std::string {
    const auto it = map.find(key);
    if (it != map.end() && !it->second.empty()) {
      return it->second;
    }
    return fallback;
  };

  std::ofstream output(path, std::ios::trunc);
  output << "BUNDLE_CLANGD=" << get("BUNDLE_CLANGD", "0") << '\n';
  output << "GDB_BUNDLE_KIND=" << get("GDB_BUNDLE_KIND", "none") << '\n';
  output << "BUNDLE_GDB=" << get("BUNDLE_GDB", "0") << '\n';
  output << "PYTHON_BUNDLE_KIND=" << get("PYTHON_BUNDLE_KIND", "none") << '\n';
  output << "BUNDLE_BASH_LS=" << get("BUNDLE_BASH_LS", "0") << '\n';
  output << "BUNDLE_TEXLAB=" << get("BUNDLE_TEXLAB", "0") << '\n';
  output << "BUNDLE_BASH_DAP=" << get("BUNDLE_BASH_DAP", "0") << '\n';
  output << "BUNDLE_RUST_ANALYZER=" << get("BUNDLE_RUST_ANALYZER", "0") << '\n';
  output << "BUNDLE_GOPLS=" << get("BUNDLE_GOPLS", "0") << '\n';
  output << "BUNDLE_ZLS=" << get("BUNDLE_ZLS", "0") << '\n';
  output << "BUNDLE_FORTLS=" << get("BUNDLE_FORTLS", "0") << '\n';
  output << "BUNDLE_LUA_LS=" << get("BUNDLE_LUA_LS", "0") << '\n';
  output << "BUNDLE_TSSERVER=" << get("BUNDLE_TSSERVER", "0") << '\n';
  output << "BUNDLE_NEOCMAKELSP=" << get("BUNDLE_NEOCMAKELSP", "0") << '\n';
  output << "BUNDLE_MAKE_LS=" << get("BUNDLE_MAKE_LS", "0") << '\n';
  output << "FORCE_BUNDLED=" << get("FORCE_BUNDLED", "0") << '\n';
  output << "UI_LOCALE=" << get("UI_LOCALE", "en") << '\n';
  output << "EDITOR_MODE=" << get("EDITOR_MODE", "normal") << '\n';
}

}  // namespace

std::optional<LspMissingPromptInfo> lsp_missing_prompt_for_status_key(const std::string& i18n_key) {
  for (const auto& entry : catalog()) {
    if (entry.status_missing_key == i18n_key) {
      return entry;
    }
  }
  return std::nullopt;
}

bool is_lsp_missing_status_key(const std::string& i18n_key) {
  return lsp_missing_prompt_for_status_key(i18n_key).has_value();
}

bool looks_like_tgdb_source_root(const std::string& path) {
  if (path.empty()) {
    return false;
  }
  std::error_code ec;
  const fs::path root(path);
  return fs::is_regular_file(root / "CMakeLists.txt", ec) &&
         fs::is_regular_file(root / "cmake" / "BundleOptions.cmake", ec) &&
         fs::is_regular_file(root / "tools" / "compile.sh", ec);
}

std::optional<std::string> find_tgdb_source_root(const std::vector<std::string>& search_roots) {
  auto walk = [](fs::path start) -> std::optional<std::string> {
    std::error_code ec;
    if (!start.empty()) {
      start = fs::absolute(start, ec);
    }
    for (int depth = 0; depth < 10 && !start.empty(); ++depth) {
      if (looks_like_tgdb_source_root(start.string())) {
        return start.string();
      }
      const fs::path parent = start.parent_path();
      if (parent == start) {
        break;
      }
      start = parent;
    }
    return std::nullopt;
  };

  for (const auto& root : search_roots) {
    if (auto found = walk(fs::path(root))) {
      return found;
    }
  }

#if defined(__linux__)
  char buffer[4096] = {};
  const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (length > 0) {
    buffer[length] = '\0';
    if (auto found = walk(fs::path(buffer).parent_path())) {
      return found;
    }
  }
#endif
  return std::nullopt;
}

bool enable_bundle_option_in_config(const std::string& tgdb_root, const LspMissingPromptInfo& info) {
  if (tgdb_root.empty() || info.bundle_config_key.empty()) {
    return false;
  }
  const fs::path config_path = fs::path(tgdb_root) / ".bundle-config";
  ConfigMap map = load_bundle_config_map(config_path);

  if (info.bundle_config_key == "PYTHON_BUNDLE_KIND") {
    const auto it = map.find("PYTHON_BUNDLE_KIND");
    // Do not downgrade a fuller Python bundle.
    if (it == map.end() || it->second == "none" || it->second.empty()) {
      map["PYTHON_BUNDLE_KIND"] = info.bundle_config_value;
    }
  } else {
    map[info.bundle_config_key] = info.bundle_config_value;
  }

  std::error_code ec;
  write_bundle_config_map(config_path, map);
  return fs::is_regular_file(config_path, ec);
}

std::string compile_command_after_bundle_config() {
  return "./tools/compile.sh -y";
}

void request_post_exit_shell(PostExitShellRequest request) {
  std::lock_guard<std::mutex> lock(g_post_exit_mutex);
  g_post_exit_request = std::move(request);
}

std::optional<PostExitShellRequest> consume_post_exit_shell_request() {
  std::lock_guard<std::mutex> lock(g_post_exit_mutex);
  std::optional<PostExitShellRequest> out = std::move(g_post_exit_request);
  g_post_exit_request.reset();
  return out;
}

std::string make_post_exit_bash_script(const PostExitShellRequest& request) {
  std::ostringstream script;
  script << "cd " << shell_single_quote(request.cwd) << " || exit 1; "
         << "printf '%s\\n' "
         << shell_single_quote(
                "tgdb: pulsa Enter para compilar con el proveedor LSP embebido "
                "(Ctrl+C cancela):")
         << "; "
         << "read -e -i " << shell_single_quote(request.command) << " LINE || exit 1; "
         << "eval \"$LINE\"";
  return script.str();
}

}  // namespace tgdb
