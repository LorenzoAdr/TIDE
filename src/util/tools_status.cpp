#include "util/tools_status.hpp"

#include <optional>

#include "dap/gdb_launcher.hpp"
#include "util/bundled_tools.hpp"

namespace tgdb {
namespace {

template <typename Location>
std::string source_label(typename Location::Source source) {
  switch (source) {
    case Location::Source::Env:
      return "env";
    case Location::Source::SystemPath:
      return "PATH";
    case Location::Source::Bundled:
      return "bundled";
  }
  return {};
}

template <typename Location>
std::string detail_from_location(const Location& loc) {
  std::string detail = loc.binary_path;
  const std::string src = source_label<Location>(loc.source);
  if (!src.empty()) {
    detail += "  [" + src + "]";
  }
  return detail;
}

ToolStatusEntry make_lsp_entry(const char* id, const char* name_key, bool enabled,
                               bool ready, bool starting,
                               const std::optional<std::string>& path_detail,
                               bool binary_available) {
  ToolStatusEntry entry;
  entry.id = id;
  entry.name_i18n_key = name_key;
  if (!enabled) {
    entry.state = ToolRuntimeState::kDisabled;
    return entry;
  }
  if (ready) {
    entry.state = ToolRuntimeState::kRunning;
    if (path_detail.has_value()) {
      entry.detail = *path_detail;
    }
    return entry;
  }
  if (starting) {
    entry.state = ToolRuntimeState::kStarting;
    if (path_detail.has_value()) {
      entry.detail = *path_detail;
    }
    return entry;
  }
  if (!binary_available) {
    entry.state = ToolRuntimeState::kUnavailable;
    return entry;
  }
  entry.state = ToolRuntimeState::kIdle;
  if (path_detail.has_value()) {
    entry.detail = *path_detail;
  }
  return entry;
}

ToolStatusEntry make_dap_entry(const char* id, const char* name_key, bool available,
                               const std::string& detail) {
  ToolStatusEntry entry;
  entry.id = id;
  entry.name_i18n_key = name_key;
  if (!available) {
    entry.state = ToolRuntimeState::kUnavailable;
    return entry;
  }
  entry.state = ToolRuntimeState::kIdle;
  entry.detail = detail;
  return entry;
}

}  // namespace

const char* tool_runtime_state_i18n_key(ToolRuntimeState state) {
  switch (state) {
    case ToolRuntimeState::kDisabled:
      return "settings.status.state.disabled";
    case ToolRuntimeState::kUnavailable:
      return "settings.status.state.unavailable";
    case ToolRuntimeState::kIdle:
      return "settings.status.state.idle";
    case ToolRuntimeState::kStarting:
      return "settings.status.state.starting";
    case ToolRuntimeState::kRunning:
      return "settings.status.state.running";
  }
  return "settings.status.state.unavailable";
}

ToolsStatusSnapshot collect_tools_status(const LspRuntimeFlags& lsp) {
  ToolsStatusSnapshot snap;

  std::optional<std::string> clangd_detail;
  bool clangd_bin = false;
  if (const auto loc = resolve_clangd(); loc.has_value()) {
    clangd_bin = true;
    clangd_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "clangd", "settings.status.tool.clangd", lsp.lsp_enabled, lsp.clangd_ready,
      lsp.clangd_starting, clangd_detail, clangd_bin));

  std::optional<std::string> py_detail;
  bool py_bin = false;
  if (const auto loc = resolve_basedpyright(); loc.has_value()) {
    py_bin = true;
    py_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "basedpyright", "settings.status.tool.basedpyright", lsp.lsp_enabled, lsp.python_ready,
      lsp.python_starting, py_detail, py_bin));

  std::optional<std::string> bash_detail;
  bool bash_bin = false;
  if (const auto loc = resolve_bash_language_server(); loc.has_value()) {
    bash_bin = true;
    bash_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "bash-ls", "settings.status.tool.bash_ls", lsp.lsp_enabled, lsp.bash_ready,
      lsp.bash_starting, bash_detail, bash_bin));

  if (const auto sc = resolve_shellcheck(); sc.has_value()) {
    ToolStatusEntry shellcheck;
    shellcheck.id = "shellcheck";
    shellcheck.name_i18n_key = "settings.status.tool.shellcheck";
    shellcheck.state = ToolRuntimeState::kIdle;
    shellcheck.detail = *sc;
    snap.language_servers.push_back(std::move(shellcheck));
  } else {
    ToolStatusEntry shellcheck;
    shellcheck.id = "shellcheck";
    shellcheck.name_i18n_key = "settings.status.tool.shellcheck";
    shellcheck.state = ToolRuntimeState::kUnavailable;
    shellcheck.detail = "";  // bash diagnostics need this
    snap.language_servers.push_back(std::move(shellcheck));
  }

  std::optional<std::string> tex_detail;
  bool tex_bin = false;
  if (const auto loc = resolve_texlab(); loc.has_value()) {
    tex_bin = true;
    tex_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "texlab", "settings.status.tool.texlab", lsp.lsp_enabled, lsp.tex_ready, lsp.tex_starting,
      tex_detail, tex_bin));

  std::optional<std::string> rust_detail;
  bool rust_bin = false;
  if (const auto loc = resolve_rust_analyzer(); loc.has_value()) {
    rust_bin = true;
    rust_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "rust-analyzer", "settings.status.tool.rust_analyzer", lsp.lsp_enabled, lsp.rust_ready,
      lsp.rust_starting, rust_detail, rust_bin));

  std::optional<std::string> go_detail;
  bool go_bin = false;
  if (const auto loc = resolve_gopls(); loc.has_value()) {
    go_bin = true;
    go_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "gopls", "settings.status.tool.gopls", lsp.lsp_enabled, lsp.go_ready, lsp.go_starting,
      go_detail, go_bin));

  std::optional<std::string> zig_detail;
  bool zig_bin = false;
  if (const auto loc = resolve_zls(); loc.has_value()) {
    zig_bin = true;
    zig_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "zls", "settings.status.tool.zls", lsp.lsp_enabled, lsp.zig_ready, lsp.zig_starting,
      zig_detail, zig_bin));

  std::optional<std::string> fortran_detail;
  bool fortran_bin = false;
  if (const auto loc = resolve_fortls(); loc.has_value()) {
    fortran_bin = true;
    fortran_detail = loc->binary_path;
    if (loc->use_python_module) {
      fortran_detail = *fortran_detail + " -m " + (loc->python_module.empty() ? "fortls" : loc->python_module);
    }
    const std::string src = source_label<FortlsLocation>(loc->source);
    if (!src.empty()) {
      *fortran_detail += "  [" + src + "]";
    }
  }
  snap.language_servers.push_back(make_lsp_entry(
      "fortls", "settings.status.tool.fortls", lsp.lsp_enabled, lsp.fortran_ready,
      lsp.fortran_starting, fortran_detail, fortran_bin));

  std::optional<std::string> lua_detail;
  bool lua_bin = false;
  if (const auto loc = resolve_lua_language_server(); loc.has_value()) {
    lua_bin = true;
    lua_detail = detail_from_location(*loc);
  }
  snap.language_servers.push_back(make_lsp_entry(
      "lua-language-server", "settings.status.tool.lua_ls", lsp.lsp_enabled, lsp.lua_ready,
      lsp.lua_starting, lua_detail, lua_bin));

  std::optional<std::string> typescript_detail;
  bool typescript_bin = false;
  if (const auto loc = resolve_typescript_language_server(); loc.has_value()) {
    typescript_bin = true;
    typescript_detail = loc->binary_path;
    if (loc->use_node_script && !loc->script_path.empty()) {
      typescript_detail = *typescript_detail + " " + loc->script_path;
    }
    const std::string src = source_label<TypescriptLsLocation>(loc->source);
    if (!src.empty()) {
      *typescript_detail += "  [" + src + "]";
    }
  }
  snap.language_servers.push_back(make_lsp_entry(
      "typescript-language-server", "settings.status.tool.typescript_ls", lsp.lsp_enabled,
      lsp.typescript_ready, lsp.typescript_starting, typescript_detail, typescript_bin));

  if (const auto chktex = resolve_chktex(); chktex.has_value()) {
    ToolStatusEntry entry;
    entry.id = "chktex";
    entry.name_i18n_key = "settings.status.tool.chktex";
    entry.state = ToolRuntimeState::kIdle;
    entry.detail = *chktex;
    snap.language_servers.push_back(std::move(entry));
  } else {
    ToolStatusEntry entry;
    entry.id = "chktex";
    entry.name_i18n_key = "settings.status.tool.chktex";
    entry.state = ToolRuntimeState::kUnavailable;
    entry.detail = "";  // texlab lint needs this
    snap.language_servers.push_back(std::move(entry));
  }

  if (const auto gdb = resolve_gdb(); gdb.has_value()) {
    if (gdb_supports_dap_at(gdb->binary_path)) {
      snap.debug_adapters.push_back(make_dap_entry("gdb", "settings.status.tool.gdb", true,
                                                     detail_from_location(*gdb)));
    } else {
      ToolStatusEntry entry;
      entry.id = "gdb";
      entry.name_i18n_key = "settings.status.tool.gdb";
      entry.state = ToolRuntimeState::kUnavailable;
      entry.detail = detail_from_location(*gdb) + " (no DAP)";
      snap.debug_adapters.push_back(std::move(entry));
    }
  } else {
    snap.debug_adapters.push_back(
        make_dap_entry("gdb", "settings.status.tool.gdb", false, {}));
  }

  if (const auto dbg = resolve_debugpy(); dbg.has_value()) {
    snap.debug_adapters.push_back(make_dap_entry(
        "debugpy", "settings.status.tool.debugpy", true,
        dbg->python_path + "  [" + source_label<DebugpyLocation>(dbg->source) + "]"));
  } else {
    snap.debug_adapters.push_back(
        make_dap_entry("debugpy", "settings.status.tool.debugpy", false, {}));
  }

  if (const auto bash_dap = resolve_bash_debug_adapter(); bash_dap.has_value()) {
    std::string detail = bash_dap->adapter_js_path;
    detail += "  [" + source_label<BashDebugAdapterLocation>(bash_dap->source) + "]";
    snap.debug_adapters.push_back(
        make_dap_entry("bashdb", "settings.status.tool.bashdb", true, detail));
  } else {
    snap.debug_adapters.push_back(
        make_dap_entry("bashdb", "settings.status.tool.bashdb", false, {}));
  }

  return snap;
}

}  // namespace tgdb
