#include "ui/ai_missing_toast.hpp"

#include <string_view>

#include "ai/ai_packages.hpp"
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

Element render_action(std::string_view label, bool selected, bool hovered, bool pressed, Box* box) {
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

bool update_hover(AiMissingToastState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->open) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kAiToastInstall, &state->install_box},
       {press_id::kAiToastIgnore, &state->ignore_box}},
      press_id::is_ai_toast_hover);
}

std::string pack_display_name(const std::string& pack_id) {
  if (const AiPackage* pack = find_ai_package(pack_id); pack != nullptr) {
    return i18n::tr(pack->name_i18n_key);
  }
  return pack_id;
}

Element render_dialog(AiMissingToastState* state, MainLayoutState* layout_state) {
  const std::string pack_name = pack_display_name(state->pack_id);

  Elements actions;
  actions.push_back(render_action(
      i18n::tr("ai_toast.action.install"), state->selected == 0,
      layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kAiToastInstall),
      layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kAiToastInstall),
      &state->install_box));
  actions.push_back(text("  "));
  actions.push_back(render_action(
      i18n::tr("ai_toast.action.ignore"), state->selected == 1,
      layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kAiToastIgnore),
      layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kAiToastIgnore),
      &state->ignore_box));

  return ModalWindow(
      text(i18n::tr("ai_toast.title")) | color(theme::Accent()),
      vbox({
          paragraphAlignLeft(i18n::tr_fmt("ai_toast.message", {pack_name})) |
              color(theme::Header()) | size(WIDTH, LESS_THAN, 56),
          text(""),
          paragraphAlignLeft(i18n::tr("ai_toast.hint")) | color(theme::Muted()) |
              size(WIDTH, LESS_THAN, 56),
          separator(),
          hbox(std::move(actions)),
          text(i18n::tr("ai_toast.footer")) | color(theme::Muted()),
      }));
}

}  // namespace

void AiMissingToastState::show(std::string pack) {
  open = true;
  pack_id = std::move(pack);
  selected = 0;
}

void AiMissingToastState::close() {
  open = false;
  selected = 0;
  pack_id.clear();
}

Component MakeAiMissingToastOverlay(Component main, AiMissingToastState* state,
                                    MainLayoutState* layout_state,
                                    std::function<void()> on_install,
                                    std::function<void()> on_ignore) {
  auto invoke_selected = [state, on_install, on_ignore]() {
    if (state == nullptr || !state->open) {
      return;
    }
    if (state->selected == 0) {
      if (on_install) {
        on_install();
      }
      return;
    }
    if (on_ignore) {
      on_ignore();
    }
  };

  return Renderer(
      CatchEvent(main,
                 [state, layout_state, on_install, on_ignore, invoke_selected](Event event) {
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
                     if (state->install_box.Contain(m.x, m.y)) {
                       state->selected = 0;
                       trigger_press(layout_state, press_id::kAiToastInstall);
                       if (on_install) {
                         on_install();
                       }
                       return true;
                     }
                     if (state->ignore_box.Contain(m.x, m.y)) {
                       state->selected = 1;
                       trigger_press(layout_state, press_id::kAiToastIgnore);
                       if (on_ignore) {
                         on_ignore();
                       }
                       return true;
                     }
                     return false;
                   }

                   if (event == Event::Escape) {
                     trigger_press(layout_state, press_id::kAiToastIgnore);
                     if (on_ignore) {
                       on_ignore();
                     }
                     return true;
                   }
                   if (event == Event::Return) {
                     invoke_selected();
                     return true;
                   }
                   if (event == Event::ArrowLeft || event == Event::Character('h')) {
                     state->selected =
                         (state->selected + state->action_count() - 1) % state->action_count();
                     return true;
                   }
                   if (event == Event::ArrowRight || event == Event::Character('l')) {
                     state->selected = (state->selected + 1) % state->action_count();
                     return true;
                   }
                   return true;
                 }),
      [main, state, layout_state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }
        return ScreenModalOverlay(std::move(base), render_dialog(state, layout_state));
      });
}

}  // namespace tuide
