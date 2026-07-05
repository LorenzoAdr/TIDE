#include "ui/right_sidebar_panel.hpp"

#include "app/app_settings.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct RightSidebarPanelState {
  Box hide_box;
};

void hide_secondary_panel(MainLayoutState* layout_state) {
  if (layout_state == nullptr || layout_state->app_settings == nullptr ||
      !layout_state->app_settings->secondary_panel_enabled) {
    return;
  }
  layout_state->app_settings->secondary_panel_enabled = false;
  layout_state->app_settings->save();
  if (layout_state->apply_app_settings_callback) {
    layout_state->apply_app_settings_callback();
  } else {
    layout_state->request_ui_tick = true;
  }
}

bool handle_sidebar_hide_mouse(RightSidebarPanelState* state, MainLayoutState* layout_state,
                               const Mouse& mouse) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }

  if (mouse.motion == Mouse::Moved) {
    return update_panel_hover(
        layout_state, mouse.x, mouse.y, {{press_id::kSidebarHide, &state->hide_box}},
        [](std::string_view id) { return id == press_id::kSidebarHide; });
  }

  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }

  if (!state->hide_box.Contain(mouse.x, mouse.y)) {
    return false;
  }

  trigger_press(layout_state, press_id::kSidebarHide);
  hide_secondary_panel(layout_state);
  return true;
}

}  // namespace

Component MakeRightSidebarPanel(Component outline, MainLayoutState* layout_state) {
  auto state = std::make_shared<RightSidebarPanelState>();

  auto panel = CatchEvent(
      Renderer(outline, [outline, layout_state, state] {
        const bool hide_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kSidebarHide);
        const bool hide_pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kSidebarHide);

        Element hide_btn = MakeToolbarButton(
            text(i18n::tr("console.hide_panel")) | color(theme::Muted()),
            hide_hovered, hide_pressed, false, &state->hide_box);

        return vbox({
                   hbox({
                       text(i18n::tr("panel.outline.title")) | color(theme::Accent()) | bold,
                       filler(),
                       hide_btn | size(WIDTH, EQUAL, 3),
                   }) | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()),
                   separator(),
                   outline->Render() | flex,
               }) |
               flex | bgcolor(theme::PanelBg());
      }),
      [layout_state, state](Event event) {
        if (!event.is_mouse()) {
          return false;
        }
        return handle_sidebar_hide_mouse(state.get(), layout_state, event.mouse());
      });

  return WrapFocusable(std::move(panel));
}

}  // namespace tgdb
