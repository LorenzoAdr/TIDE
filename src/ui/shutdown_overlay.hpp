#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

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

ftxui::Element RenderShutdownDialog(const ShutdownState::Snapshot& state);
ftxui::Element RenderShutdownFullScreen(const ShutdownState::Snapshot& state);

}  // namespace tgdb
