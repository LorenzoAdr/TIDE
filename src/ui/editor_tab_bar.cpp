#include "ui/editor_tab_bar.hpp"

#include <algorithm>
#include <filesystem>

#include "app/editor_tabs.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
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

}  // namespace

Element make_editor_tab_bar(WorkspaceModel* workspace, EditorTabBarState* state) {
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

  if (visible.has_overflow) {
    const std::string overflow_label = "+" + std::to_string(visible.hidden_count);
    Element cell = text(" " + overflow_label + " ") | color(theme::Accent());
    if (state->overflow_open) {
      cell = cell | inverted;
    }
    cells.push_back(cell | bgcolor(theme::TabIdle()));
  }

  for (int i = visible.start; i < visible.end; ++i) {
    const auto& tab = workspace->tabs[static_cast<std::size_t>(i)];
    const bool active = i == workspace->active_tab;
    const std::string label = tab_label(tab, 12);

    Element tab_cell = text(" " + label) | color(active ? theme::Header() : theme::Muted());
    Element close_cell = text(" x") | color(theme::Muted());
    if (active) {
      tab_cell = tab_cell | bgcolor(theme::TabActive());
      close_cell = close_cell | bgcolor(theme::TabActive());
    } else {
      tab_cell = tab_cell | bgcolor(theme::TabIdle());
      close_cell = close_cell | bgcolor(theme::TabIdle());
    }
    if (state->dragging && state->drag_tab == i) {
      tab_cell = tab_cell | dim;
    }
    if (state->dragging && state->drag_target == i && state->drag_tab != i) {
      tab_cell = tab_cell | color(theme::Accent()) | bold;
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
      Element row = text(" " + line) | color(theme::Header());
      if (i == workspace->active_tab) {
        row = row | bold | color(theme::Accent());
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
                          EditorTabBarState* state, const Mouse& m) {
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
    state->overflow_open = !state->overflow_open;
    state->overflow_selected = std::max(0, workspace->active_tab);
    return true;
  }
  if (hit.kind == TabBarHitKind::Close) {
    workspace->close_tab(hit.tab_index);
    return true;
  }
  if (hit.kind == TabBarHitKind::Tab) {
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
