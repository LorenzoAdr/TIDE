#include "ui/editor_tab_bar.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <filesystem>

#include "app/editor_tabs.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui/hover_effects.hpp"
#include "ui/clickable.hpp"
#include "ui/press_ids.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string tab_label(const EditorTab& tab) {
  std::string name = tab.path.empty() ? i18n::tr("editor.tab.untitled")
                                      : std::filesystem::path(tab.path).filename().string();
  if (tab.external) {
    name = "+" + name;
  }
  if (tab.buffer.dirty) {
    name += "*";
  }
  if (static_cast<int>(name.size()) > kEditorTabLabelMaxLen) {
    name = name.substr(0, static_cast<std::size_t>(kEditorTabLabelMaxLen - 1)) + "…";
  }
  return name;
}

std::string format_tab_label_area(const EditorTab& tab) {
  std::string area = " " + tab_label(tab);
  if (static_cast<int>(area.size()) < kEditorTabLabelAreaWidth) {
    area.append(static_cast<std::size_t>(kEditorTabLabelAreaWidth - area.size()), ' ');
  }
  return area;
}

bool lookup_tab_hit(EditorTabBarState* state, WorkspaceModel* workspace, int x, int y,
                    TabBarHit* out) {
  if (state == nullptr || workspace == nullptr || out == nullptr) {
    return false;
  }
  if (state->bar_box.IsEmpty() || !state->bar_box.Contain(x, y)) {
    return false;
  }
  if (!state->layout_valid) {
    return false;
  }

  int cx = state->bar_box.x_min;

  if (state->layout_visible.has_overflow) {
    if (x >= cx && x <= cx + state->layout_overflow_width - 1) {
      *out = {TabBarHitKind::Overflow, -1, Box{}};
      return true;
    }
    cx += state->layout_overflow_width;
  }

  for (int i = state->layout_visible.start; i < state->layout_visible.end; ++i) {
    const int tab_start = cx;
    const int tab_end = tab_start + kEditorTabCellWidth - 1;
    if (x >= tab_start && x <= tab_end) {
      if (x >= tab_start + kEditorTabLabelAreaWidth) {
        *out = {TabBarHitKind::Close, i, Box{}};
      } else {
        *out = {TabBarHitKind::Tab, i, Box{}};
      }
      return true;
    }
    cx += kEditorTabCellWidth;
  }
  return false;
}

int tab_index_at(EditorTabBarState* state, WorkspaceModel* workspace, int x, int y) {
  TabBarHit hit;
  if (!lookup_tab_hit(state, workspace, x, y, &hit) || hit.kind != TabBarHitKind::Tab) {
    return -1;
  }
  return hit.tab_index;
}

Element style_tab_cell(Element cell, bool active, bool hovered, bool pressed) {
  ClickableState state{active, hovered, pressed, false};
  return StyleClickable(std::move(cell), state);
}

Element style_tab_close_cell(Element cell, bool active, bool hovered, bool pressed) {
  Color fg = theme::Muted();
  if (hovered || pressed) {
    fg = theme::Error();
  }
  cell = cell | color(fg);
  if (pressed) {
    return cell | bold | inverted | bgcolor(theme::TabPressed());
  }
  if (hovered) {
    return cell | bold | bgcolor(theme::TabHover());
  }
  if (active) {
    return cell | bgcolor(theme::TabActive());
  }
  return cell | bgcolor(theme::TabIdle());
}

std::string hover_id_for_hit(const TabBarHit& hit) {
  switch (hit.kind) {
    case TabBarHitKind::Overflow:
      return std::string(press_id::kEditorTabOverflow);
    case TabBarHitKind::Tab:
      return press_id::editor_tab(hit.tab_index);
    case TabBarHitKind::Close:
      return press_id::editor_tab_close(hit.tab_index);
  }
  return {};
}

bool tab_hit_interaction(const ClickableInteractionTracker* clickable, const TabBarHit& hit) {
  if (clickable == nullptr) {
    return false;
  }
  const std::string id = hover_id_for_hit(hit);
  return clickable->is_hovered(id) || clickable->is_pressed(id);
}

}  // namespace

Element make_editor_tab_bar(WorkspaceModel* workspace, EditorTabBarState* state,
                            MainLayoutState* layout_state) {
  if (workspace == nullptr || state == nullptr) {
    return text("");
  }

  workspace->flush_active_tab();
  const int tab_count = static_cast<int>(workspace->tabs.size());
  if (tab_count == 0) {
    state->layout_valid = false;
    return text(" ") | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
           reflect(state->bar_box);
  }

  const int bar_width = std::max(20, state->bar_width_chars);
  const TabVisibleRange visible =
      compute_visible_tab_range(tab_count, workspace->active_tab, bar_width);

  state->layout_visible = visible;
  state->layout_overflow_width = visible.has_overflow ? kEditorTabOverflowWidth : 0;
  state->layout_bar_width = bar_width;
  state->layout_tab_count = tab_count;
  state->layout_active_tab = workspace->active_tab;
  state->layout_valid = true;

  Elements cells;

  const ClickableInteractionTracker* clickable =
      layout_state != nullptr ? &layout_state->clickable : nullptr;

  if (visible.has_overflow) {
    Element cell = text(format_editor_tab_overflow_button(visible.hidden_count)) |
                   color(theme::TitleText());
    if (state->overflow_open) {
      cell = cell | inverted;
    }
    const TabBarHit overflow_hit{TabBarHitKind::Overflow, -1, Box{}};
    const bool hovered = tab_hit_interaction(clickable, overflow_hit);
    const bool pressed =
        clickable != nullptr && clickable->is_pressed(press_id::kEditorTabOverflow);
    cell = style_tab_cell(std::move(cell), state->overflow_open, hovered, pressed);
    cells.push_back(cell);
  }

  for (int i = visible.start; i < visible.end; ++i) {
    const auto& tab = workspace->tabs[static_cast<std::size_t>(i)];
    const bool active = i == workspace->active_tab;
    const std::string tab_id = press_id::editor_tab(i);
    const std::string close_id = press_id::editor_tab_close(i);

    Element tab_cell = text(format_tab_label_area(tab));
    Element close_cell = text(" × ");
    const bool tab_hovered = clickable != nullptr && clickable->is_hovered(tab_id);
    const bool tab_pressed = clickable != nullptr && clickable->is_pressed(tab_id);
    const bool close_pressed = clickable != nullptr && clickable->is_pressed(close_id);
    const bool close_hovered = state->hover_close_tab_index == i || close_pressed;

    tab_cell = style_tab_cell(std::move(tab_cell), active, tab_hovered, tab_pressed);
    close_cell = style_tab_close_cell(std::move(close_cell), active, close_hovered, close_pressed);
    if (state->dragging && state->drag_tab == i) {
      tab_cell = tab_cell | dim;
    }
    if (state->dragging && state->drag_target == i && state->drag_tab != i) {
      tab_cell = tab_cell | color(theme::TitleText()) | bold;
    }
    cells.push_back(hbox({std::move(tab_cell), std::move(close_cell)}));
  }

  // Un solo reflect en la barra; los hits se calculan al pulsar con bar_box ya actualizado.
  return hbox(std::move(cells)) | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
         reflect(state->bar_box);
}

Element make_tabs_overflow_modal(WorkspaceModel* workspace, EditorTabBarState* state) {
  if (workspace == nullptr || state == nullptr || !state->overflow_open) {
    return text("");
  }

  workspace->flush_active_tab();
  Elements rows;
  const int count = static_cast<int>(workspace->tabs.size());
  if (count == 0) {
    rows.push_back(text(i18n::tr("editor.tab.no_open_tabs")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < count; ++i) {
      const auto& tab = workspace->tabs[static_cast<std::size_t>(i)];
      std::string line = tab.path.empty() ? i18n::tr("editor.tab.no_path") : tab.path;
      if (tab.buffer.dirty) {
        line += " *";
      }
      Element row = text(" " + line) | color(theme::FileText());
      if (i == workspace->active_tab) {
        row = row | bold | color(theme::TitleText());
      }
      if (i == state->overflow_selected) {
        row = row | inverted | bgcolor(theme::EditorLineHi());
      } else {
        row = row | bgcolor(theme::PanelBg());
      }
      rows.push_back(row);
    }
  }

  Element dialog = ModalWindow(
      text(i18n::tr("editor.tab.overflow.title")) | color(theme::Accent()),
      vbox({vbox(std::move(rows)) | size(HEIGHT, LESS_THAN, 12),
            text(i18n::tr("editor.tab.overflow.footer")) | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

bool handle_tab_bar_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                          EditorTabBarState* state, const Mouse& m,
                          MainLayoutState* layout_state, FocusRegion panel_focus) {
  if (workspace == nullptr || state == nullptr) {
    return false;
  }

  if (state->dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      state->drag_target = tab_index_at(state, workspace, m.x, m.y);
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      const int from = state->drag_tab;
      const int to = state->drag_target;
      state->dragging = false;
      state->drag_tab = -1;
      state->drag_target = -1;
      if (from >= 0 && to >= 0 && from != to) {
        workspace->move_tab(from, to);
      }
      return true;
    }
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
  }

  TabBarHit hit;
  if (!lookup_tab_hit(state, workspace, m.x, m.y, &hit)) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = panel_focus;
  }
  if (hit.kind == TabBarHitKind::Overflow) {
    trigger_press(layout_state, press_id::kEditorTabOverflow);
    state->overflow_open = !state->overflow_open;
    state->overflow_selected = std::max(0, workspace->active_tab);
    return true;
  }
  if (hit.kind == TabBarHitKind::Close) {
    trigger_press(layout_state, press_id::editor_tab_close(hit.tab_index));
    workspace->close_tab(hit.tab_index);
    return true;
  }
  if (hit.kind == TabBarHitKind::Tab) {
    trigger_press(layout_state, press_id::editor_tab(hit.tab_index));
    if (hit.tab_index != workspace->active_tab) {
      workspace->switch_to_tab(hit.tab_index);
    }
    state->drag_tab = hit.tab_index;
    state->drag_target = hit.tab_index;
    state->dragging = true;
    return true;
  }

  return false;
}

Element make_tab_hover_tooltip(WorkspaceModel* workspace, const EditorTabBarState* state) {
  if (workspace == nullptr || state == nullptr || state->hover_tab_index < 0) {
    return text("");
  }
  if (state->hover_tab_index >= static_cast<int>(workspace->tabs.size())) {
    return text("");
  }
  const std::string& path = workspace->tabs[static_cast<std::size_t>(state->hover_tab_index)].path;
  if (path.empty() || state->bar_box.IsEmpty()) {
    return text("");
  }

  const std::string filename = std::filesystem::path(path).filename().string();
  Element popup =
      vbox({text(" " + filename) | bold | color(theme::TitleText()),
            text(" " + path) | color(theme::Muted())}) |
      border | bgcolor(theme::PanelBg());

  const int rel_x = std::max(0, state->hover_x - state->bar_box.x_min + 1);
  const int y_below = state->bar_box.y_max - state->bar_box.y_min + 1;
  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_below),
                     hbox({filler() | size(WIDTH, EQUAL, rel_x), popup | clear_under, filler()}),
                     filler()}) |
                   flex});
}

bool update_editor_chrome_hover(WorkspaceModel* workspace, EditorTabBarState* state,
                                MainLayoutState* layout_state, const Box& problems_box, int x,
                                int y) {
  if (!hover_effects_enabled()) {
    return false;
  }
  if (layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const int before_close = state != nullptr ? state->hover_close_tab_index : -1;
  TabBarHit hit;
  if (state != nullptr && lookup_tab_hit(state, workspace, x, y, &hit)) {
    state->hover_x = x;
    state->hover_y = y;
    if (hit.kind == TabBarHitKind::Close) {
      state->hover_close_tab_index = hit.tab_index;
      state->hover_tab_index = -1;
    } else if (hit.kind == TabBarHitKind::Tab) {
      state->hover_close_tab_index = -1;
      state->hover_tab_index = hit.tab_index;
    } else {
      state->hover_close_tab_index = -1;
      state->hover_tab_index = -1;
    }
    layout_state->clickable.set_hover(hover_id_for_hit(hit));
  } else {
    if (state != nullptr) {
      state->hover_tab_index = -1;
      state->hover_close_tab_index = -1;
    }
    if (!problems_box.IsEmpty() && problems_box.Contain(x, y)) {
      layout_state->clickable.set_hover(press_id::kEditorProblems);
    }
    // No limpiar hover de chrome aquí: el handler del otro editor puede haberlo
    // establecido ya en este mismo evento de ratón.
  }
  if (layout_state->clickable.hovered_id() != before ||
      (state != nullptr && state->hover_close_tab_index != before_close)) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

bool handle_tabs_overflow_keys(WorkspaceModel* workspace, FocusManagerState* focus,
                               EditorTabBarState* state, const Event& event,
                               FocusRegion panel_focus) {
  if (workspace == nullptr || state == nullptr || !state->overflow_open) {
    return false;
  }

  const int count = static_cast<int>(workspace->tabs.size());
  if (event == Event::Escape) {
    state->overflow_open = false;
    return true;
  }
  if (count == 0) {
    return event == Event::Escape;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->overflow_selected = std::min(state->overflow_selected + 1, count - 1);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->overflow_selected = std::max(0, state->overflow_selected - 1);
    return true;
  }
  if (event == Event::Return) {
    workspace->switch_to_tab(state->overflow_selected);
    state->overflow_open = false;
    if (focus != nullptr) {
      focus->region = panel_focus;
    }
    return true;
  }
  if (event == Event::Character('x')) {
    workspace->close_tab(state->overflow_selected);
    if (workspace->tabs.empty()) {
      state->overflow_open = false;
      state->overflow_selected = 0;
      return true;
    }
    state->overflow_selected =
        std::max(0, std::min(state->overflow_selected, static_cast<int>(workspace->tabs.size()) - 1));
    return true;
  }
  return false;
}

}  // namespace tgdb
