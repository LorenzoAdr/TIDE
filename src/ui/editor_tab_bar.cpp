#include "ui/editor_tab_bar.hpp"

#include <algorithm>
#include <filesystem>

#include "app/editor_tabs.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ui/clickable.hpp"
#include "ui/press_ids.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string tab_label(const EditorTab& tab, int max_len) {
  std::string name = tab.path.empty() ? "Sin título"
                                      : std::filesystem::path(tab.path).filename().string();
  if (tab.buffer.dirty) {
    name += "*";
  }
  if (static_cast<int>(name.size()) > max_len) {
    name = name.substr(0, static_cast<std::size_t>(max_len - 1)) + "…";
  }
  return name;
}

bool lookup_tab_hit(EditorTabBarState* state, WorkspaceModel* workspace, int x, int y,
                    TabBarHit* out) {
  if (state == nullptr || workspace == nullptr || out == nullptr) {
    return false;
  }
  if (state->bar_box.IsEmpty() || !state->bar_box.Contain(x, y)) {
    return false;
  }

  workspace->flush_active_tab();
  const int tab_count = static_cast<int>(workspace->tabs.size());
  if (tab_count == 0) {
    return false;
  }

  const int bar_width = std::max(20, state->bar_width_chars);
  const TabVisibleRange visible =
      compute_visible_tab_range(tab_count, workspace->active_tab, bar_width);

  int cx = state->bar_box.x_min;

  if (visible.has_overflow) {
    const std::string overflow_label = "+" + std::to_string(visible.hidden_count);
    const int w = static_cast<int>(overflow_label.size()) + 2;
    if (x >= cx && x <= cx + w - 1) {
      *out = {TabBarHitKind::Overflow, -1, Box{}};
      return true;
    }
    cx += w;
  }

  for (int i = visible.start; i < visible.end; ++i) {
    const auto& tab = workspace->tabs[static_cast<std::size_t>(i)];
    const std::string label = tab_label(tab, 12);
    const int tab_w = static_cast<int>(label.size()) + 1;
    const int close_w = 2;
    if (x >= cx && x <= cx + tab_w - 1) {
      *out = {TabBarHitKind::Tab, i, Box{}};
      return true;
    }
    cx += tab_w;
    if (x >= cx && x <= cx + close_w - 1) {
      *out = {TabBarHitKind::Close, i, Box{}};
      return true;
    }
    cx += close_w;
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
    return text(" ") | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
           reflect(state->bar_box);
  }

  const int bar_width = std::max(20, state->bar_width_chars);
  const TabVisibleRange visible =
      compute_visible_tab_range(tab_count, workspace->active_tab, bar_width);

  Elements cells;

  const ClickableInteractionTracker* clickable =
      layout_state != nullptr ? &layout_state->clickable : nullptr;

  if (visible.has_overflow) {
    const std::string overflow_label = "+" + std::to_string(visible.hidden_count);
    Element cell = text(" " + overflow_label + " ") | color(theme::TitleText());
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
    const std::string label = tab_label(tab, 12);
    const std::string tab_id = press_id::editor_tab(i);
    const std::string close_id = press_id::editor_tab_close(i);

    Element tab_cell = text(" " + label);
    Element close_cell = text(" x");
    const bool tab_hovered = clickable != nullptr && clickable->is_hovered(tab_id);
    const bool tab_pressed = clickable != nullptr && clickable->is_pressed(tab_id);
    const bool close_hovered = clickable != nullptr && clickable->is_hovered(close_id);
    const bool close_pressed = clickable != nullptr && clickable->is_pressed(close_id);

    tab_cell = style_tab_cell(std::move(tab_cell), active, tab_hovered, tab_pressed);
    close_cell = style_tab_cell(std::move(close_cell), active, close_hovered, close_pressed);
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
    rows.push_back(text(" (sin pestañas abiertas) ") | color(theme::Muted()));
  } else {
    for (int i = 0; i < count; ++i) {
      const auto& tab = workspace->tabs[static_cast<std::size_t>(i)];
      std::string line = tab.path.empty() ? "(sin ruta)" : tab.path;
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
      text("Archivos abiertos") | color(theme::Accent()),
      vbox({vbox(std::move(rows)) | size(HEIGHT, LESS_THAN, 12),
            text(" Enter abrir  x cerrar  Esc cancelar") | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

bool handle_tab_bar_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                          EditorTabBarState* state, const Mouse& m,
                          MainLayoutState* layout_state) {
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
    focus->region = FocusRegion::Editor;
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

bool update_editor_chrome_hover(WorkspaceModel* workspace, EditorTabBarState* state,
                                MainLayoutState* layout_state, const Box& problems_box, int x,
                                int y) {
  if (layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  TabBarHit hit;
  if (state != nullptr && lookup_tab_hit(state, workspace, x, y, &hit)) {
    layout_state->clickable.set_hover(hover_id_for_hit(hit));
  } else if (!problems_box.IsEmpty() && problems_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::kEditorProblems);
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_editor_chrome_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

bool handle_tabs_overflow_keys(WorkspaceModel* workspace, FocusManagerState* focus,
                               EditorTabBarState* state, const Event& event) {
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
      focus->region = FocusRegion::Editor;
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
