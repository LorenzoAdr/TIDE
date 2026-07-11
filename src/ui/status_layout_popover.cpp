#include "ui/status_layout_popover.hpp"
#include "ui/ui_wake.hpp"

#include <string>

#include "ftxui/component/mouse.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/main_layout.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kPopoverRows = 7;

std::string checkbox_row_label(bool checked, const char* label_key) {
  return i18n::tr_fmt(checked ? "settings.checkbox.checked" : "settings.checkbox.unchecked",
                     {i18n::tr(label_key)});
}

bool files_visible(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->explorer_visible;
}

bool outline_visible(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->app_settings != nullptr &&
         layout_state->app_settings->secondary_panel_enabled;
}

bool terminal_visible(const MainLayoutState* layout_state) {
  return layout_state != nullptr && layout_state->console_visible;
}

void set_files_visible(MainLayoutState* layout_state, bool visible) {
  if (layout_state == nullptr || layout_state->status_set_files_visible == nullptr) {
    return;
  }
  layout_state->status_set_files_visible(visible);
}

void set_outline_visible(MainLayoutState* layout_state, bool visible) {
  if (layout_state == nullptr || layout_state->status_set_outline_visible == nullptr) {
    return;
  }
  layout_state->status_set_outline_visible(visible);
}

void set_terminal_visible(MainLayoutState* layout_state, bool visible) {
  if (layout_state == nullptr || layout_state->status_set_terminal_visible == nullptr) {
    return;
  }
  layout_state->status_set_terminal_visible(visible);
}

bool layout_popover_hover_id(std::string_view id) {
  return id == press_id::kStatusLayoutFiles || id == press_id::kStatusLayoutOutline ||
         id == press_id::kStatusLayoutTerminal;
}

bool toggle_layout_row(MainLayoutState* layout_state, std::string_view row_id) {
  if (layout_state == nullptr) {
    return false;
  }
  if (row_id == press_id::kStatusLayoutFiles) {
    set_files_visible(layout_state, !files_visible(layout_state));
    return true;
  }
  if (row_id == press_id::kStatusLayoutOutline) {
    set_outline_visible(layout_state, !outline_visible(layout_state));
    return true;
  }
  if (row_id == press_id::kStatusLayoutTerminal) {
    set_terminal_visible(layout_state, !terminal_visible(layout_state));
    return true;
  }
  return false;
}

}  // namespace

Element RenderStatusLayoutPopoverOverlay(StatusLayoutPopoverState* popover,
                                         MainLayoutState* layout_state, Element base,
                                         const Box& anchor_box) {
  if (popover == nullptr || !popover->open || anchor_box.IsEmpty()) {
    return base;
  }

  const auto row = [&](const char* label_key, bool checked, std::string_view row_id, Box* box) {
    const bool hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
    const bool pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
    Element line = text(" " + checkbox_row_label(checked, label_key) + " ") |
                   color(theme::Header()) | bgcolor(theme::PanelBg());
    line = StyleListRow(std::move(line), false, hovered, pressed);
    return line | reflect(*box);
  };

  Element menu =
      vbox({
          text(" " + i18n::tr("status.layout.title") + " ") | bold | color(theme::Accent()) |
              bgcolor(theme::PanelBg()),
          separator() | color(theme::AccentDim()),
          row("status.layout.files", files_visible(layout_state), press_id::kStatusLayoutFiles,
              &popover->files_row_box),
          row("status.layout.outline", outline_visible(layout_state),
              press_id::kStatusLayoutOutline, &popover->outline_row_box),
          row("status.layout.terminal", terminal_visible(layout_state),
              press_id::kStatusLayoutTerminal, &popover->terminal_row_box),
      }) |
      border | bgcolor(theme::PanelBg()) | reflect(popover->menu_box);

  const int y_top = std::max(0, anchor_box.y_min - kPopoverRows);
  const int x_left = anchor_box.x_min;

  return dbox({
      std::move(base),
      vbox({
          filler() | size(HEIGHT, EQUAL, y_top),
          hbox({
              filler() | size(WIDTH, EQUAL, x_left),
              std::move(menu) | clear_under,
              filler(),
          }),
          filler(),
      }) | flex,
  });
}

bool HandleStatusLayoutPopoverMouse(StatusLayoutPopoverState* popover,
                                    MainLayoutState* layout_state, const Box& anchor_box,
                                    Event event) {
  if (popover == nullptr || layout_state == nullptr || !popover->open || !event.is_mouse()) {
    return false;
  }

  const Mouse& mouse = event.mouse();

  if (mouse.motion == Mouse::Moved) {
    return update_panel_hover(
        layout_state, mouse.x, mouse.y,
        {{press_id::kStatusLayoutFiles, &popover->files_row_box},
         {press_id::kStatusLayoutOutline, &popover->outline_row_box},
         {press_id::kStatusLayoutTerminal, &popover->terminal_row_box}},
        layout_popover_hover_id);
  }

  if (mouse.button != Mouse::Left || mouse.motion != Mouse::Pressed) {
    return false;
  }

  if (popover->files_row_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLayoutFiles);
    toggle_layout_row(layout_state, press_id::kStatusLayoutFiles);
    return true;
  }
  if (popover->outline_row_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLayoutOutline);
    toggle_layout_row(layout_state, press_id::kStatusLayoutOutline);
    return true;
  }
  if (popover->terminal_row_box.Contain(mouse.x, mouse.y)) {
    trigger_press(layout_state, press_id::kStatusLayoutTerminal);
    toggle_layout_row(layout_state, press_id::kStatusLayoutTerminal);
    return true;
  }

  if (popover->menu_box.Contain(mouse.x, mouse.y)) {
    return true;
  }
  if (anchor_box.Contain(mouse.x, mouse.y)) {
    popover->open = false;
    layout_state->clickable.clear_hover_if(layout_popover_hover_id);
    UI_WAKE(layout_state, "wake");
    return true;
  }

  popover->open = false;
  layout_state->clickable.clear_hover_if(layout_popover_hover_id);
  UI_WAKE(layout_state, "wake");
  return true;
}

bool HandleStatusLayoutPopoverKeys(StatusLayoutPopoverState* popover, Event event) {
  if (popover == nullptr || !popover->open) {
    return false;
  }
  if (event == Event::Escape) {
    popover->open = false;
    return true;
  }
  return false;
}

}  // namespace tgdb
