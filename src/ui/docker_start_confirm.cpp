#include "ui/docker_start_confirm.hpp"

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

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kYes = 0;
constexpr int kNo = 1;

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

bool update_hover(DockerStartConfirmState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->open) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kDockerStartYes, &state->yes_box}, {press_id::kDockerStartNo, &state->no_box}},
      press_id::is_docker_start_hover);
}

}  // namespace

void DockerStartConfirmState::show(const std::string& container_name) {
  open = true;
  selected = kYes;
  container = container_name;
}

void DockerStartConfirmState::close() {
  open = false;
  selected = kYes;
  container.clear();
}

Component MakeDockerStartConfirmOverlay(Component main, DockerStartConfirmState* state,
                                        MainLayoutState* layout_state,
                                        std::function<void()> on_confirm,
                                        std::function<void()> on_cancel) {
  return Renderer(
      CatchEvent(main, [state, layout_state, on_confirm, on_cancel](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_hover(state, layout_state, event.mouse().x, event.mouse().y);
          return false;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->yes_box.Contain(m.x, m.y)) {
            state->selected = kYes;
            trigger_press(layout_state, press_id::kDockerStartYes);
            state->close();
            if (on_confirm) {
              on_confirm();
            }
            return true;
          }
          if (state->no_box.Contain(m.x, m.y)) {
            state->selected = kNo;
            trigger_press(layout_state, press_id::kDockerStartNo);
            state->close();
            if (on_cancel) {
              on_cancel();
            }
            return true;
          }
        }

        if (event == Event::Escape) {
          state->close();
          if (on_cancel) {
            on_cancel();
          }
          return true;
        }
        if (event == Event::Return) {
          if (state->selected == kYes) {
            trigger_press(layout_state, press_id::kDockerStartYes);
            state->close();
            if (on_confirm) {
              on_confirm();
            }
          } else {
            trigger_press(layout_state, press_id::kDockerStartNo);
            state->close();
            if (on_cancel) {
              on_cancel();
            }
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
      [main, state, layout_state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        const bool yes_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kDockerStartYes);
        const bool no_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kDockerStartNo);
        const bool yes_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kDockerStartYes);
        const bool no_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kDockerStartNo);

        Element dialog = ModalWindow(
            text(i18n::tr("modal.docker_start.title")) | color(theme::Accent()),
            vbox({
                text(i18n::tr_fmt("modal.docker_start.question", {state->container})) |
                    color(theme::Header()),
                separator(),
                hbox({
                    render_choice(i18n::tr("common.yes"), state->selected == kYes, yes_hovered,
                                  yes_pressed, &state->yes_box),
                    text("  "),
                    render_choice(i18n::tr("common.no"), state->selected == kNo, no_hovered,
                                  no_pressed, &state->no_box),
                }),
                text(i18n::tr("common.footer.confirm_esc")) | color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tuide
