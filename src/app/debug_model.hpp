#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "backend/idebug_backend.hpp"

namespace tgdb {

struct CoreAnalyzerInstance {
  std::string address_hex;
  std::uintptr_t address = 0;
  std::size_t size = 0;
  std::string type_name;
  std::string reference_summary;
};

struct WatchEntry {
  std::string expression;
  std::string value;
};

struct HardwareWatchEntry {
  std::string expression;
  std::string label;
  bool enabled = true;
  int gdb_number = -1;
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
  std::string status_message;
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
  std::vector<std::string> core_analyzer_log;

  SessionMode session_mode = SessionMode::kLaunch;
  CoreAnalysisMode core_analysis_mode = CoreAnalysisMode::kGdbOnly;
  std::string core_path;
  bool is_post_mortem = false;
  std::string core_analyzer_search_query;
  std::vector<CoreAnalyzerInstance> core_analyzer_instances;
  int core_analyzer_selected_instance = -1;

  std::unordered_map<std::string, std::unordered_set<int>> breakpoints_by_file;
  std::vector<HardwareWatchEntry> hardware_watches;

  int active_thread_id = 1;
  std::string stop_reason;

  std::string source_substitute_from;
  std::string source_substitute_to;

  // Incrementado al cambiar active_file para forzar recarga del panel de código.
  uint64_t view_token = 0;

  void append_console(const std::string& line);
  void append_core_analyzer_log(const std::string& line);
  void set_stopped(int thread_id, const std::string& reason);
  void set_running();
  void set_terminated();
  void toggle_breakpoint(const std::string& file, int line);
  bool has_breakpoint(const std::string& file, int line) const;
  bool is_breakpoint_enabled(const std::string& file, int line) const;
  void set_breakpoint_enabled(const std::string& file, int line, bool enabled);
  void remove_breakpoint(const std::string& file, int line);
  void clear_all_breakpoints();
  std::vector<int> enabled_breakpoint_lines(const std::string& file) const;
  void add_watch(const std::string& expression);
  void remove_watch(int index);
  void remove_watch(const std::string& expression);
  void add_hardware_watch(const std::string& expression, const std::string& label);
  void remove_hardware_watch(int index);
  void set_hardware_watch_enabled(int index, bool enabled);
  bool is_hardware_watch_enabled(int index) const;
  void clear_hardware_watches();

  std::unordered_map<std::string, std::unordered_set<int>> disabled_breakpoints;
};

}  // namespace tgdb
