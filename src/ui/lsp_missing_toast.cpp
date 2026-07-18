#include "ui/lsp_missing_toast.hpp"

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

bool update_hover(LspMissingToastState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->open) {
    return false;
  }
  if (state->show_bundle_action) {
    return update_panel_hover(
        layout_state, x, y,
        {{press_id::kLspToastInstall, &state->install_box},
         {press_id::kLspToastBundle, &state->bundle_box},
         {press_id::kLspToastIgnore, &state->ignore_box}},
        press_id::is_lsp_toast_hover);
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kLspToastInstall, &state->install_box},
       {press_id::kLspToastIgnore, &state->ignore_box}},
      press_id::is_lsp_toast_hover);
}

Element render_dialog(LspMissingToastState* state, MainLayoutState* layout_state) {
  const std::string language = i18n::tr(state->info.language_i18n_key);

  Elements actions;
  const bool install_sel = state->selected == 0;
  actions.push_back(render_action(
      i18n::tr("lsp_toast.action.install"), install_sel,
      layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kLspToastInstall),
      layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kLspToastInstall),
      &state->install_box));
  actions.push_back(text("  "));

  int ignore_index = 1;
  if (state->show_bundle_action) {
    const bool bundle_sel = state->selected == 1;
    actions.push_back(render_action(
        i18n::tr("lsp_toast.action.bundle"), bundle_sel,
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kLspToastBundle),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kLspToastBundle),
        &state->bundle_box));
    actions.push_back(text("  "));
    ignore_index = 2;
  }

  const bool ignore_sel = state->selected == ignore_index;
  actions.push_back(render_action(
      i18n::tr("lsp_toast.action.ignore"), ignore_sel,
      layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kLspToastIgnore),
      layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kLspToastIgnore),
      &state->ignore_box));

  return ModalWindow(
      text(i18n::tr("lsp_toast.title")) | color(theme::Accent()),
      vbox({
          paragraphAlignLeft(i18n::tr_fmt("lsp_toast.message", {language})) | color(theme::Header()) |
              size(WIDTH, LESS_THAN, 56),
          text(""),
          paragraphAlignLeft(i18n::tr("lsp_toast.hint")) | color(theme::Muted()) |
              size(WIDTH, LESS_THAN, 56),
          separator(),
          hbox(std::move(actions)),
          text(i18n::tr("lsp_toast.footer")) | color(theme::Muted()),
      }));
}

}  // namespace

void LspMissingToastState::show(LspMissingPromptInfo prompt, bool can_bundle) {
  open = true;
  info = std::move(prompt);
  show_bundle_action = can_bundle && !info.bundle_config_key.empty();
  selected = 0;
}

void LspMissingToastState::close() {
  open = false;
  selected = 0;
  show_bundle_action = false;
  info = {};
}

int LspMissingToastState::action_count() const {
  return show_bundle_action ? 3 : 2;
}

Component MakeLspMissingToastOverlay(Component main, LspMissingToastState* state,
                                     MainLayoutState* layout_state,
                                     std::function<void()> on_install,
                                     std::function<void()> on_bundle,
                                     std::function<void()> on_ignore) {
  auto invoke_selected = [state, on_install, on_bundle, on_ignore]() {
    if (state == nullptr || !state->open) {
      return;
    }
    if (state->selected == 0) {
      if (on_install) {
        on_install();
      }
      return;
    }
    if (state->show_bundle_action && state->selected == 1) {
      if (on_bundle) {
        on_bundle();
      }
      return;
    }
    if (on_ignore) {
      on_ignore();
    }
  };

  return Renderer(
      CatchEvent(main,
                 [state, layout_state, on_install, on_bundle, on_ignore, invoke_selected](Event event) {
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
                       trigger_press(layout_state, press_id::kLspToastInstall);
                       if (on_install) {
                         on_install();
                       }
                       return true;
                     }
                     if (state->show_bundle_action && state->bundle_box.Contain(m.x, m.y)) {
                       state->selected = 1;
                       trigger_press(layout_state, press_id::kLspToastBundle);
                       if (on_bundle) {
                         on_bundle();
                       }
                       return true;
                     }
                     if (state->ignore_box.Contain(m.x, m.y)) {
                       state->selected = state->action_count() - 1;
                       trigger_press(layout_state, press_id::kLspToastIgnore);
                       if (on_ignore) {
                         on_ignore();
                       }
                       return true;
                     }
                     return false;
                   }

                   if (event == Event::Escape) {
                     // Esc == Ignorar: cierra y silencia el resto de avisos LSP missing.
                     trigger_press(layout_state, press_id::kLspToastIgnore);
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
                   // Modal abierto: no dejar pasar teclas/ratón a la terminal u otros paneles.
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
