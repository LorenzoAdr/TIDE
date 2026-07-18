#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace tuide {

enum class UiEventKind {
  UserInput,
  InputCorrelated,
  TerminalOutput,
  DebugCritical,
};

struct UiEvent {
  UiEventKind kind = UiEventKind::UserInput;
  uint64_t correlation_id = 0;
  std::string tag;
  std::function<void()> pre_paint;
  const char* src_file = "";
  int src_line = 0;
};

struct UiEventDrainPlan {
  bool run_terminal = false;
  bool run_debug = false;
  bool run_editor = false;
  bool run_ui_tasks = false;
  bool run_full_background = false;
  bool lsp_diag_only = false;
  bool lsp_completion_only = false;
};

const char* ui_event_kind_label(UiEventKind kind);

}  // namespace tuide
