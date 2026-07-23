#pragma once

#include <functional>
#include <string>
#include <vector>

namespace tuide {

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

struct BreakpointInfo {
  std::string file;
  int line = 0;
  bool verified = false;
  std::string message;
};

struct LaunchConfig {
  std::string program;
  std::string cwd;
  std::vector<std::string> args;
  bool stop_at_main = false;
  bool packet_monitor_enabled = false;
  std::string packet_monitor_filter_src;
  std::string packet_monitor_filter_dst;
};

struct AttachConfig {
  std::string program;
  std::string cwd;
  int pid = 0;
  std::string target;
};

enum class CoreAnalysisMode { kGdbOnly, kCoreAnalyzer };

struct CoreConfig {
  std::string program;
  std::string core_path;
  std::string cwd;
  CoreAnalysisMode analysis = CoreAnalysisMode::kGdbOnly;
};

enum class SessionMode { kLaunch, kAttach, kCore };

enum class EvaluateContext { kRepl, kWatch, kCoreAnalyzer, kHover };

enum class UiCommandKind {
  kConnect,
  kLaunch,
  kAttach,
  kLoadCore,
  kContinue,
  kPause,
  kNext,
  kStepIn,
  kStepOut,
  kEvaluate,
  kSetBreakpoints,
  kSyncBreakpoints,
  kRefreshStack,
  kFetchVariables,
  kFetchVariableChildren,
  kAddWatch,
  kSetWatchValue,
  kAddHardwareWatch,
  kRemoveHardwareWatch,
  kSetHardwareWatchEnabled,
  kDisconnect,
  kDetach,
  kSetSourceSubstitutePath,
  kQuit,
};

struct UiCommand {
  UiCommandKind kind = UiCommandKind::kQuit;
  LaunchConfig launch;
  AttachConfig attach;
  CoreConfig core;
  std::string expression;
  EvaluateContext evaluate_context = EvaluateContext::kRepl;
  std::string correlation_id;
  std::string assign_value;
  std::string breakpoint_file;
  std::vector<int> breakpoint_lines;
  int frame_id = -1;
  int thread_id = 1;
  int variables_reference = 0;
  std::string parent_expression;
  int variable_depth = 0;
  int hardware_watch_index = -1;
  bool hardware_watch_enabled = true;
  int hardware_watch_gdb_number = -1;
  std::string substitute_from;
  std::string substitute_to;
};

enum class DebugEventKind {
  kSessionReady,
  kLaunchConfigured,
  kOutput,
  kStopped,
  kContinued,
  kTerminated,
  kStackUpdated,
  kVariablesUpdated,
  kEvaluateResult,
  kCoreAnalyzerResult,
  kHoverValue,
  kWatchUpdated,
  kVariableChildrenUpdated,
  kError,
  kBreakpointsUpdated,
  kHardwareWatchUpdated,
  kInferiorPid,
};

struct DebugEvent {
  DebugEventKind kind = DebugEventKind::kError;
  std::string text;
  // DAP OutputEvent.category: console | stdout | stderr | telemetry | …
  std::string output_category;
  uint64_t backend_epoch = 0;
  int inferior_pid = 0;
  int thread_id = 1;
  std::string stop_reason;
  std::vector<StackFrameInfo> stack_frames;
  std::vector<VariableInfo> variables;
  std::string parent_expression;
  std::string watch_expression;
  std::string hover_key;
  std::string hover_expression;
  std::string hover_value;
  int stack_frame_id = -1;
  std::string watch_value;
  std::vector<BreakpointInfo> breakpoints;
  int hardware_watch_index = -1;
  int hardware_watch_gdb_number = -1;
};

class IDebugBackend {
 public:
  virtual ~IDebugBackend() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void submit(const UiCommand& command) = 0;
};

}  // namespace tuide
