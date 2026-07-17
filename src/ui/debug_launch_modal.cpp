#include "ui/debug_launch_modal.hpp"

#include <string_view>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string title_for_mode(SessionMode mode) {
  switch (mode) {
    case SessionMode::kAttach:
      return i18n::tr("modal.debug_launch.title_attach");
    case SessionMode::kCore:
      return i18n::tr("modal.debug_launch.title_core");
    case SessionMode::kLaunch:
    default:
      return i18n::tr("modal.debug_launch.title_launch");
  }
}

std::string action_label(const DebugLaunchModalState& state) {
  if (state.phase == DebugLaunchModalPhase::Error) {
    return i18n::tr("modal.debug_launch.close");
  }
  return i18n::tr("modal.debug_launch.cancel");
}

std::string_view action_press_id(const DebugLaunchModalState& state) {
  if (state.phase == DebugLaunchModalPhase::Error) {
    return press_id::kDebugLaunchClose;
  }
  return press_id::kDebugLaunchCancel;
}

Element render_action(std::string_view label, bool hovered, bool pressed, Box* box) {
  Element row = text(std::string(label)) | color(theme::Header());
  if (pressed) {
    row = row | inverted | bold | bgcolor(theme::TabPressed());
  } else if (hovered) {
    row = row | bold | bgcolor(theme::TabHover());
  } else {
    row = row | inverted | bold;
  }
  return row | reflect(*box);
}

bool update_hover(DebugLaunchModalState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->open()) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y, {{action_press_id(*state), &state->action_box}},
      [](std::string_view id) { return press_id::is_debug_launch_hover(id); });
}

}  // namespace

Component MakeDebugLaunchModalOverlay(Component main, DebugLaunchModalState* state,
                                      MainLayoutState* layout_state, ShutdownState* shutdown_state,
                                      std::function<void()> on_cancel,
                                      std::function<void()> on_dismiss_error) {
  return Renderer(
      CatchEvent(main,
                 [state, layout_state, shutdown_state, on_cancel, on_dismiss_error](Event event) {
                   if (state == nullptr || !state->open() ||
                       (shutdown_state != nullptr && shutdown_state->is_active())) {
                     return false;
                   }

                   if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
                     update_hover(state, layout_state, event.mouse().x, event.mouse().y);
                     return false;
                   }

                   const auto invoke_action = [&] {
                     const bool is_error = state->phase == DebugLaunchModalPhase::Error;
                     trigger_press(layout_state, action_press_id(*state));
                     if (is_error) {
                       if (on_dismiss_error) {
                         on_dismiss_error();
                       }
                     } else if (on_cancel) {
                       on_cancel();
                     }
                   };

                   if (event.is_mouse() && event.mouse().button == Mouse::Left &&
                       event.mouse().motion == Mouse::Pressed) {
                     const auto& m = event.mouse();
                     if (state->action_box.Contain(m.x, m.y)) {
                       invoke_action();
                       return true;
                     }
                   }

                   if (event == Event::Escape || event == Event::Return) {
                     invoke_action();
                     return true;
                   }
                   return true;
                 }),
      [main, state, layout_state, shutdown_state] {
        if (shutdown_state != nullptr && shutdown_state->is_active()) {
          return main->Render();
        }
        Element base = main->Render();
        if (state == nullptr || !state->open()) {
          return base;
        }

        const std::string_view press = action_press_id(*state);
        const bool hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press);
        const bool pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press);

        Elements body;
        if (!state->program.empty()) {
          body.push_back(text(state->program) | color(theme::Muted()));
        }
        body.push_back(text(state->message.empty() ? i18n::tr("modal.debug_launch.working")
                                                   : state->message) |
                       color(theme::Header()) | bold);
        if (!state->detail.empty()) {
          body.push_back(text(state->detail) | color(theme::Error()) | size(HEIGHT, LESS_THAN, 6));
        }
        body.push_back(separator());
        body.push_back(render_action(action_label(*state), hovered, pressed, &state->action_box));
        body.push_back(text(state->phase == DebugLaunchModalPhase::Error
                                ? i18n::tr("modal.debug_launch.footer_close")
                                : i18n::tr("modal.debug_launch.footer_cancel")) |
                       color(theme::Muted()));

        const Color title_color =
            state->phase == DebugLaunchModalPhase::Error ? theme::Error() : theme::Accent();
        Element dialog =
            ModalWindow(text(title_for_mode(state->session_mode)) | color(title_color),
                        vbox(std::move(body)));
        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
