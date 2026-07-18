#pragma once

#include <string>
#include <vector>

namespace tgdb {

enum class ToolRuntimeState {
  kDisabled,     // feature off (e.g. LSP disabled in settings)
  kUnavailable,  // binary / adapter not found
  kIdle,         // found on disk, not launched yet
  kStarting,     // launch in progress
  kRunning,      // process ready / serving
};

struct ToolStatusEntry {
  std::string id;             // stable id: "clangd", "bash-ls", ...
  std::string name_i18n_key;  // settings.status.tool.*
  ToolRuntimeState state = ToolRuntimeState::kUnavailable;
  std::string detail;         // path or short note
};

struct ToolsStatusSnapshot {
  std::vector<ToolStatusEntry> language_servers;
  std::vector<ToolStatusEntry> debug_adapters;
};

struct LspRuntimeFlags {
  bool lsp_enabled = true;
  bool clangd_ready = false;
  bool clangd_starting = false;
  bool python_ready = false;
  bool python_starting = false;
  bool bash_ready = false;
  bool bash_starting = false;
  bool tex_ready = false;
  bool tex_starting = false;
  bool rust_ready = false;
  bool rust_starting = false;
  bool go_ready = false;
  bool go_starting = false;
  bool zig_ready = false;
  bool zig_starting = false;
  bool fortran_ready = false;
  bool fortran_starting = false;
  bool lua_ready = false;
  bool lua_starting = false;
  bool typescript_ready = false;
  bool typescript_starting = false;
  bool cmake_ready = false;
  bool cmake_starting = false;
  bool make_ready = false;
  bool make_starting = false;
};

// Resolves binaries and combines with live LSP runtime flags.
ToolsStatusSnapshot collect_tools_status(const LspRuntimeFlags& lsp);

const char* tool_runtime_state_i18n_key(ToolRuntimeState state);

}  // namespace tgdb
