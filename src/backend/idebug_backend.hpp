#pragma once

#include <functional>
#include <string>
#include <vector>

#include "app/debug_model.hpp"

namespace tgdb {

struct LaunchConfig {
  std::string program;
  std::string cwd;
  std::vector<std::string> args;
  bool stop_at_main = true;
};

struct AttachConfig {
  std::string program;
  std::string cwd;
  int pid = 0;
  std::string target;
};

enum class SessionMode { kLaunch, kAttach };

enum class EvaluateContext { kRepl, kWatch };

enum class UiCommandKind {
  kConnect,
  kLaunch,
  kAttach,
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
  kDisconnect,
  kDetach,
  kQuit,
};

struct UiCommand {
  UiCommandKind kind = UiCommandKind::kQuit;
  LaunchConfig launch;
  AttachConfig attach;
  std::string expression;
  EvaluateContext evaluate_context = EvaluateContext::kRepl;
  std::string assign_value;
  std::string breakpoint_file;
  std::vector<int> breakpoint_lines;
  int frame_id = -1;
  int thread_id = 1;
  int variables_reference = 0;
  std::string parent_expression;
  int variable_depth = 0;
};

enum class DebugEventKind {
  kSessionReady,
  kOutput,
  kStopped,
  kContinued,
  kTerminated,
  kStackUpdated,
  kVariablesUpdated,
  kEvaluateResult,
  kWatchUpdated,
  kVariableChildrenUpdated,
  kError,
  kBreakpointsUpdated,
};

struct DebugEvent {
  DebugEventKind kind = DebugEventKind::kError;
  std::string text;
  int thread_id = 1;
  std::string stop_reason;
  std::vector<StackFrameInfo> stack_frames;
  std::vector<VariableInfo> variables;
  std::string parent_expression;
  std::string watch_expression;
  int stack_frame_id = -1;
  std::string watch_value;
  std::vector<BreakpointInfo> breakpoints;
};

class IDebugBackend {
 public:
  virtual ~IDebugBackend() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void submit(const UiCommand& command) = 0;
};

}  // namespace tgdb
