#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "app/debug_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"
#include "ui/shutdown_overlay.hpp"

namespace tgdb {

enum class DebugLaunchModalPhase {
  Closed,
  Connecting,
  Starting,
  Error,
};

struct DebugLaunchModalState {
  DebugLaunchModalPhase phase = DebugLaunchModalPhase::Closed;
  SessionMode session_mode = SessionMode::kLaunch;
  std::string program;
  std::string message;
  std::string detail;
  uint64_t generation = 0;
  ftxui::Box action_box;

  bool open() const { return phase != DebugLaunchModalPhase::Closed; }
  bool is_generation(uint64_t gen) const { return open() && generation == gen; }

  void reset() {
    phase = DebugLaunchModalPhase::Closed;
    program.clear();
    message.clear();
    detail.clear();
  }
};

ftxui::Component MakeDebugLaunchModalOverlay(ftxui::Component main, DebugLaunchModalState* state,
                                             MainLayoutState* layout_state,
                                             ShutdownState* shutdown_state,
                                             std::function<void()> on_cancel,
                                             std::function<void()> on_dismiss_error);

}  // namespace tgdb
