#pragma once

#include <functional>
#include <string>

#include "backend/idebug_backend.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

class DebugUiChannel {
 public:
  explicit DebugUiChannel(MainLayoutState* layout) : layout_(layout) {}

  void on_debug_event(DebugEventKind kind, std::function<void()> pre_paint = {});

 private:
  MainLayoutState* layout_;
};

inline void DebugUiChannel::on_debug_event(DebugEventKind kind, std::function<void()> pre_paint) {
  if (layout_ == nullptr || layout_->ui_events == nullptr) {
    if (pre_paint) {
      pre_paint();
    }
    return;
  }
  std::string tag = "debug.event";
  switch (kind) {
    case DebugEventKind::kStopped:
      tag = "debug.stopped";
      break;
    case DebugEventKind::kTerminated:
      tag = "debug.terminated";
      break;
    case DebugEventKind::kSessionReady:
      tag = "debug.session_ready";
      break;
    case DebugEventKind::kLaunchConfigured:
      tag = "debug.launch_configured";
      break;
    case DebugEventKind::kContinued:
      tag = "debug.continued";
      break;
    case DebugEventKind::kStackUpdated:
      tag = "debug.stack_updated";
      break;
    case DebugEventKind::kVariablesUpdated:
      tag = "debug.variables_updated";
      break;
    case DebugEventKind::kWatchUpdated:
      tag = "debug.watch_updated";
      break;
    case DebugEventKind::kVariableChildrenUpdated:
      tag = "debug.variable_children_updated";
      break;
    case DebugEventKind::kHoverValue:
      tag = "debug.hover_value";
      break;
    case DebugEventKind::kHardwareWatchUpdated:
      tag = "debug.hardware_watch_updated";
      break;
    default:
      break;
  }
  layout_->ui_events->emit_debug(std::move(tag), std::move(pre_paint), __FILE__, __LINE__);
}

}  // namespace tuide
