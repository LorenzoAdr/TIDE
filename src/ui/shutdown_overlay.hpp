#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

struct ShutdownState {
  void begin(int total_steps);
  void set_current(const std::string& message);
  void complete_step(const std::string& message);
  void mark_complete();

  bool is_active() const;
  bool is_complete() const;

  struct Snapshot {
    bool active = false;
    bool complete = false;
    int completed_steps = 0;
    int total_steps = 0;
    std::string current_step;
    std::vector<std::string> trace;
  };

  Snapshot snapshot() const;

 private:
  mutable std::mutex mutex_;
  bool active_ = false;
  bool complete_ = false;
  int completed_steps_ = 0;
  int total_steps_ = 0;
  std::string current_step_;
  std::vector<std::string> trace_;
};

struct ShutdownOverlayState {
  ftxui::Box force_exit_box;
};

ftxui::Element RenderShutdownDialog(const ShutdownState::Snapshot& state, MainLayoutState* layout,
                                    ShutdownOverlayState* overlay);
ftxui::Element RenderShutdownFullScreen(const ShutdownState::Snapshot& state,
                                        MainLayoutState* layout, ShutdownOverlayState* overlay);

ftxui::Component MakeShutdownOverlay(ftxui::Component main, ShutdownState* shutdown_state,
                                     ShutdownOverlayState* overlay_state,
                                     MainLayoutState* layout_state,
                                     std::function<void()> on_force_exit);

}  // namespace tuide
