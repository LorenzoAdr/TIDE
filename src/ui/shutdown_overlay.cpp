#include "ui/shutdown_overlay.hpp"

#include <algorithm>

#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/spinner.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kProgressBarWidth = 28;
constexpr std::size_t kMaxTraceLines = 8;

std::string progress_bar(int completed, int total) {
  if (total <= 0) {
    return std::string(kProgressBarWidth, '-');
  }
  const int filled =
      std::clamp((completed * kProgressBarWidth) / total, 0, kProgressBarWidth);
  std::string bar;
  bar.reserve(static_cast<std::size_t>(kProgressBarWidth));
  for (int i = 0; i < kProgressBarWidth; ++i) {
    bar.push_back(i < filled ? '#' : '-');
  }
  return bar;
}

}  // namespace

void ShutdownState::begin(int total_steps) {
  std::lock_guard<std::mutex> lock(mutex_);
  active_ = true;
  complete_ = false;
  completed_steps_ = 0;
  total_steps_ = std::max(0, total_steps);
  current_step_.clear();
  trace_.clear();
}

void ShutdownState::set_current(const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_step_ = message;
}

void ShutdownState::complete_step(const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  trace_.push_back(message);
  if (trace_.size() > kMaxTraceLines) {
    trace_.erase(trace_.begin());
  }
  ++completed_steps_;
  current_step_.clear();
}

void ShutdownState::mark_complete() {
  std::lock_guard<std::mutex> lock(mutex_);
  complete_ = true;
  current_step_ = "Listo";
}

bool ShutdownState::is_active() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_;
}

bool ShutdownState::is_complete() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return complete_;
}

ShutdownState::Snapshot ShutdownState::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return Snapshot{active_, complete_, completed_steps_, total_steps_, current_step_, trace_};
}

Element RenderShutdownDialog(const ShutdownState::Snapshot& snap) {
  Elements trace_rows;
  for (const auto& line : snap.trace) {
    trace_rows.push_back(text("  ok  " + line) | color(theme::Muted()));
  }
  if (!snap.current_step.empty()) {
    trace_rows.push_back(hbox({
        text("  " + spinner::glyph() + "  ") | color(theme::Accent()),
        text(snap.current_step) | color(theme::Header()),
    }));
  }

  const int progress_steps = snap.complete ? snap.total_steps : snap.completed_steps;
  const std::string bar = progress_bar(progress_steps, snap.total_steps);
  const std::string progress_label =
      snap.total_steps > 0
          ? std::to_string(progress_steps) + "/" + std::to_string(snap.total_steps)
          : "";

  return ModalWindow(
      text("Cerrando tide") | color(theme::Accent()),
      vbox({
          text("Cerrando sesión...") | color(theme::Header()),
          separator(),
          hbox({
              text(bar) | color(theme::Accent()),
              text("  "),
              text(progress_label) | color(theme::Muted()),
          }),
          separator(),
          vbox(std::move(trace_rows)),
      }));
}

Element RenderShutdownFullScreen(const ShutdownState::Snapshot& snap) {
  Element backdrop =
      filler() | size(WIDTH, EQUAL, 0) | size(HEIGHT, EQUAL, 0) | bgcolor(theme::PanelBg());
  Element dialog = clear_under(RenderShutdownDialog(snap));
  return dbox({std::move(backdrop), center(std::move(dialog))});
}

}  // namespace tgdb
