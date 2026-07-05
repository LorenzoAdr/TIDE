#include "ui/quit_confirm.hpp"

#include <string_view>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kYes = 0;
constexpr int kNo = 1;

void close_quit_confirm(QuitConfirmState* state) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->selected = kYes;
  state->unsaved_paths.clear();
}

Element render_choice(std::string_view label, bool selected, bool hovered, bool pressed, Box* box) {
  Element row = text(std::string(label)) | color(theme::Header());
  if (pressed) {
    row = row | inverted | bold | bgcolor(theme::TabPressed());
  } else if (hovered) {
    row = row | bold | bgcolor(theme::TabHover());
  } else if (selected) {
    row = row | inverted | bold;
  }
  return row | reflect(*box);
}

bool update_quit_hover(QuitConfirmState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->open) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kQuitYes, &state->yes_box}, {press_id::kQuitNo, &state->no_box}},
      press_id::is_quit_hover);
}

}  // namespace

Component MakeQuitConfirmOverlay(Component main, QuitConfirmState* state,
                                 MainLayoutState* layout_state, ShutdownState* shutdown_state,
                                 std::function<void()> on_confirm) {
  return Renderer(
      CatchEvent(main, [state, layout_state, shutdown_state, on_confirm](Event event) {
        if (state == nullptr || !state->open ||
            (shutdown_state != nullptr && shutdown_state->is_active())) {
          return false;
        }

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_quit_hover(state, layout_state, event.mouse().x, event.mouse().y);
          return false;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->yes_box.Contain(m.x, m.y)) {
            state->selected = kYes;
            trigger_press(layout_state, press_id::kQuitYes);
            close_quit_confirm(state);
            if (on_confirm) {
              on_confirm();
            }
            return true;
          }
          if (state->no_box.Contain(m.x, m.y)) {
            state->selected = kNo;
            trigger_press(layout_state, press_id::kQuitNo);
            close_quit_confirm(state);
            return true;
          }
        }

        if (event == Event::Escape) {
          close_quit_confirm(state);
          return true;
        }
        if (event == Event::Return) {
          if (state->selected == kYes) {
            trigger_press(layout_state, press_id::kQuitYes);
            close_quit_confirm(state);
            if (on_confirm) {
              on_confirm();
            }
          } else {
            trigger_press(layout_state, press_id::kQuitNo);
            close_quit_confirm(state);
          }
          return true;
        }
        if (event == Event::ArrowLeft || event == Event::Character('h')) {
          state->selected = kYes;
          return true;
        }
        if (event == Event::ArrowRight || event == Event::Character('l')) {
          state->selected = kNo;
          return true;
        }
        if (event == Event::Character('s') || event == Event::Character('S')) {
          state->selected = kYes;
          return true;
        }
        if (event == Event::Character('n') || event == Event::Character('N')) {
          state->selected = kNo;
          return true;
        }
        return true;
      }),
      [main, state, layout_state, shutdown_state] {
        if (shutdown_state != nullptr && shutdown_state->is_active()) {
          return main->Render();
        }
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        const bool yes_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kQuitYes);
        const bool no_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kQuitNo);
        const bool yes_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kQuitYes);
        const bool no_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kQuitNo);

        Elements body;
        if (state->unsaved_paths.empty()) {
          body.push_back(text(i18n::tr("modal.quit.question")) | color(theme::Header()));
        } else {
          body.push_back(text(i18n::tr("modal.quit.unsaved_header")) | color(theme::Header()));
          Elements file_rows;
          for (const auto& path : state->unsaved_paths) {
            file_rows.push_back(
                text(i18n::tr_fmt("common.highlight.wrap", {path})) | color(theme::Muted()));
          }
          body.push_back(vbox(std::move(file_rows)) | size(HEIGHT, LESS_THAN, 10));
          body.push_back(text(i18n::tr("modal.quit.unsaved_warning")) | color(theme::Header()));
        }
        body.push_back(separator());
        body.push_back(hbox({
            render_choice(i18n::tr("common.yes"), state->selected == kYes, yes_hovered, yes_pressed,
                          &state->yes_box),
            text("  "),
            render_choice(i18n::tr("common.no"), state->selected == kNo, no_hovered, no_pressed,
                          &state->no_box),
        }));
        body.push_back(text(i18n::tr("common.footer.confirm_esc")) | color(theme::Muted()));

        Element dialog = ModalWindow(text(i18n::tr("modal.quit.title")) | color(theme::Accent()),
                                     vbox(std::move(body)));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
