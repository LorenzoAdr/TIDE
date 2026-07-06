#include "ui/shutdown_overlay.hpp"

#include <algorithm>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
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

bool update_shutdown_hover(ShutdownOverlayState* overlay, MainLayoutState* layout_state, int x,
                           int y) {
  if (overlay == nullptr || layout_state == nullptr) {
    return false;
  }
  return update_panel_hover(layout_state, x, y,
                            {{press_id::kShutdownForceExit, &overlay->force_exit_box}},
                            press_id::is_shutdown_hover);
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
  current_step_ = i18n::tr("modal.shutdown.ready");
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

Element RenderShutdownDialog(const ShutdownState::Snapshot& snap, MainLayoutState* layout,
                             ShutdownOverlayState* overlay) {
  Elements trace_rows;
  for (const auto& line : snap.trace) {
    trace_rows.push_back(text(i18n::tr_fmt("modal.shutdown.step_ok", {line})) | color(theme::Muted()));
  }
  if (!snap.current_step.empty()) {
    trace_rows.push_back(hbox({
        text(i18n::tr_fmt("modal.shutdown.spinner_prefix", {spinner::glyph()})) |
            color(theme::Accent()),
        text(snap.current_step) | color(theme::Header()),
    }));
  }

  const int progress_steps = snap.complete ? snap.total_steps : snap.completed_steps;
  const std::string bar = progress_bar(progress_steps, snap.total_steps);
  const std::string progress_label =
      snap.total_steps > 0 ? i18n::tr_fmt("modal.shutdown.progress",
                                          {std::to_string(progress_steps),
                                           std::to_string(snap.total_steps)})
                         : "";

  const bool force_hovered =
      layout != nullptr && layout->clickable.is_hovered(press_id::kShutdownForceExit);
  const bool force_pressed =
      layout != nullptr && layout->clickable.is_pressed(press_id::kShutdownForceExit);

  Elements body{
      text(i18n::tr("modal.shutdown.message")) | color(theme::Header()),
      separator(),
      hbox({
          text(bar) | color(theme::Accent()),
          text("  "),
          text(progress_label) | color(theme::Muted()),
      }),
      separator(),
      vbox(std::move(trace_rows)),
      separator(),
      center(MakeToolbarButton(text(i18n::tr("modal.shutdown.force_exit")) | color(theme::Header()),
                               force_hovered, force_pressed, false,
                               overlay != nullptr ? &overlay->force_exit_box : nullptr)),
  };

  return ModalWindow(
      text(i18n::tr("modal.shutdown.title")) | color(theme::Accent()),
      vbox(std::move(body)));
}

Element RenderShutdownFullScreen(const ShutdownState::Snapshot& snap, MainLayoutState* layout,
                                 ShutdownOverlayState* overlay) {
  Element backdrop =
      filler() | size(WIDTH, EQUAL, 0) | size(HEIGHT, EQUAL, 0) | bgcolor(theme::PanelBg());
  Element dialog = clear_under(RenderShutdownDialog(snap, layout, overlay));
  return dbox({std::move(backdrop), center(std::move(dialog))});
}

Component MakeShutdownOverlay(Component main, ShutdownState* shutdown_state,
                              ShutdownOverlayState* overlay_state, MainLayoutState* layout_state,
                              std::function<void()> on_force_exit) {
  return Renderer(
      CatchEvent(main, [shutdown_state, overlay_state, layout_state, on_force_exit](Event event) {
        if (shutdown_state == nullptr || !shutdown_state->is_active()) {
          return false;
        }

        if (event == Event::Custom) {
          cursor_blink::tick();
          if (layout_state != nullptr) {
            layout_state->clickable.tick();
          }
          return false;
        }

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_shutdown_hover(overlay_state, layout_state, event.mouse().x, event.mouse().y);
          return true;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (overlay_state != nullptr && overlay_state->force_exit_box.Contain(m.x, m.y)) {
            trigger_press(layout_state, press_id::kShutdownForceExit);
            if (on_force_exit) {
              on_force_exit();
            }
            return true;
          }
        }

        return true;
      }),
      [main, shutdown_state, overlay_state, layout_state] {
        if (shutdown_state != nullptr && shutdown_state->is_active()) {
          return RenderShutdownFullScreen(shutdown_state->snapshot(), layout_state, overlay_state);
        }
        return main->Render();
      });
}

}  // namespace tgdb
