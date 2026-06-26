#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace tgdb {

struct StackFrameInfo {
  int id = 0;
  std::string name;
  std::string file;
  int line = 0;
};

struct VariableInfo {
  std::string name;
  std::string expression;
  std::string value;
  std::string type;
  int variables_reference = 0;
  int depth = 0;
};

struct WatchEntry {
  std::string expression;
  std::string value;
};

struct BreakpointInfo {
  std::string file;
  int line = 0;
  bool verified = false;
  std::string message;
};

enum class DebugState {
  kDisconnected,
  kConnecting,
  kRunning,
  kStopped,
  kTerminated,
};

struct DebugModel {
  DebugState state = DebugState::kDisconnected;
  std::string status_message = "Desconectado";
  std::string workspace_root;
  std::string program;
  std::vector<std::string> program_args;

  std::string active_file;
  int active_line = 0;
  int selected_frame = 0;
  int variables_frame_id = -1;

  std::vector<StackFrameInfo> stack_frames;
  std::vector<VariableInfo> locals;
  std::unordered_map<std::string, std::vector<VariableInfo>> variable_children;
  std::vector<WatchEntry> watches;
  std::vector<std::string> console_output;

  std::unordered_map<std::string, std::unordered_set<int>> breakpoints_by_file;

  int active_thread_id = 1;
  std::string stop_reason;

  // Incrementado al cambiar active_file para forzar recarga del panel de código.
  uint64_t view_token = 0;

  void append_console(const std::string& line);
  void set_stopped(int thread_id, const std::string& reason);
  void set_running();
  void set_terminated();
  void toggle_breakpoint(const std::string& file, int line);
  bool has_breakpoint(const std::string& file, int line) const;
  bool is_breakpoint_enabled(const std::string& file, int line) const;
  void set_breakpoint_enabled(const std::string& file, int line, bool enabled);
  void remove_breakpoint(const std::string& file, int line);
  std::vector<int> enabled_breakpoint_lines(const std::string& file) const;
  void add_watch(const std::string& expression);

  std::unordered_map<std::string, std::unordered_set<int>> disabled_breakpoints;
};

}  // namespace tgdb
