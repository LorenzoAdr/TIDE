#include "ui/outline_panel.hpp"

#include "util/ui_panel_render_cache.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/focusable_component.hpp"
#include "ui/glyphs.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll_offset(int total_lines, int visible_lines) {
  return std::max(0, total_lines - visible_lines);
}

std::pair<std::string, std::string> split_kind_prefix(const std::string& name) {
  static const char* const prefixes[] = {"ns ", "C ", "S ", "M ", "v ", "f "};
  for (const char* prefix : prefixes) {
    const std::size_t len = std::strlen(prefix);
    if (name.size() >= len && name.compare(0, len, prefix) == 0) {
      return {prefix, name.substr(len)};
    }
  }
  return {"", name};
}

std::optional<std::pair<std::string, std::string>> split_scope_name(const std::string& body) {
  const auto pos = body.rfind("::");
  if (pos == std::string::npos) {
    return std::nullopt;
  }
  return std::make_pair(body.substr(0, pos), body.substr(pos + 2));
}

int count_consecutive_scope(const std::vector<SymbolInfo>& symbols, int start,
                            const std::string& scope) {
  int count = 0;
  for (int i = start; i < static_cast<int>(symbols.size()); ++i) {
    const auto [kind_prefix, body] = split_kind_prefix(symbols[static_cast<std::size_t>(i)].name);
    (void)kind_prefix;
    const auto split = split_scope_name(body);
    if (!split || split->first != scope) {
      break;
    }
    ++count;
  }
  return count;
}

enum class OutlineRowKind { Scope, Symbol };

struct OutlineDisplayRow {
  OutlineRowKind kind = OutlineRowKind::Symbol;
  int symbol_index = -1;
  std::string label;
  int indent = 0;
  SymbolKind color_kind = SymbolKind::kFunction;
};

struct OutlinePanelState {
  std::vector<SymbolInfo> symbols;
  std::vector<OutlineDisplayRow> display_rows;
  std::string loaded_file;
  bool symbols_fetch_pending = false;
  uint64_t last_document_symbols_revision = 0;
  int selected = 0;
  int list_scroll = 0;
  int last_visible_lines = 1;
  Box content_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;

  void rebuild_display_rows() {
    display_rows.clear();
    std::string active_scope;
    bool scope_header_shown = false;

    for (int i = 0; i < static_cast<int>(symbols.size()); ++i) {
      const auto& sym = symbols[static_cast<std::size_t>(i)];
      const auto [kind_prefix, body] = split_kind_prefix(sym.name);
      const auto scope_split = split_scope_name(body);
      const int base_indent = sym.depth * 2;

      if (!scope_split) {
        active_scope.clear();
        scope_header_shown = false;
        display_rows.push_back(
            {OutlineRowKind::Symbol, i, sym.name, base_indent, sym.kind});
        continue;
      }

      const std::string& scope = scope_split->first;
      const std::string short_label = kind_prefix + scope_split->second;

      if (scope != active_scope) {
        active_scope = scope;
        scope_header_shown = false;
        if (count_consecutive_scope(symbols, i, scope) >= 2) {
          display_rows.push_back({OutlineRowKind::Scope,
                                  -1,
                                  kind_prefix + scope + ":",
                                  base_indent,
                                  SymbolKind::kNamespace});
          scope_header_shown = true;
        }
      }

      if (scope_header_shown) {
        display_rows.push_back(
            {OutlineRowKind::Symbol, i, short_label, base_indent + 2, sym.kind});
      } else {
        display_rows.push_back(
            {OutlineRowKind::Symbol, i, sym.name, base_indent, sym.kind});
      }
    }
  }

  int display_row_count() const { return static_cast<int>(display_rows.size()); }

  int nearest_symbol_row(int from, int direction) const {
    if (display_rows.empty()) {
      return 0;
    }
    int row = from;
    for (;;) {
      row += direction;
      if (row < 0) {
        return 0;
      }
      if (row >= display_row_count()) {
        return display_row_count() - 1;
      }
      if (display_rows[static_cast<std::size_t>(row)].kind == OutlineRowKind::Symbol) {
        return row;
      }
    }
  }

  void clamp_scroll() {
    const int visible = visible_line_count(content_box);
    list_scroll = std::max(0, std::min(list_scroll, max_scroll_offset(display_row_count(), visible)));
  }

  void scroll_row_into_view(int row) {
    const int visible = visible_line_count(content_box);
    if (row < list_scroll) {
      list_scroll = row;
    } else if (row >= list_scroll + visible) {
      list_scroll = row - visible + 1;
    }
    clamp_scroll();
  }

  std::optional<int> row_at_mouse(int x, int y) const {
    if (!content_box.Contain(x, y)) {
      return std::nullopt;
    }
    const int row = list_scroll + (y - content_box.y_min);
    if (row < 0 || row >= display_row_count()) {
      return std::nullopt;
    }
    return row;
  }
};

void fetch_outline_symbols(OutlinePanelState* state, ISymbolProvider* symbols,
                           WorkspaceModel* workspace, MainLayoutState* layout_state) {
  if (state == nullptr || symbols == nullptr || !state->symbols_fetch_pending ||
      state->loaded_file.empty()) {
    return;
  }
  state->symbols = symbols->symbols_for_file(state->loaded_file);
  state->rebuild_display_rows();
  state->symbols_fetch_pending = false;
  if (layout_state != nullptr) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
    layout_state->request_ui_tick = true;
  }
  if (symbols->symbols_lsp_pending(state->loaded_file)) {
    return;
  }
  if (state->selected >= state->display_row_count()) {
    state->selected = std::max(0, state->display_row_count() - 1);
  }
  if (!state->display_rows.empty() &&
      state->display_rows[static_cast<std::size_t>(state->selected)].kind == OutlineRowKind::Scope) {
    state->selected = state->nearest_symbol_row(state->selected, 1);
  }
}

bool update_outline_hover(OutlinePanelState* state, MainLayoutState* layout_state, int x, int y) {
  if (!hover_effects_enabled()) {
    return false;
  }
  if (layout_state == nullptr || state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto row = state->row_at_mouse(x, y);
  if (row.has_value()) {
    layout_state->clickable.set_hover(press_id::outline_row(*row));
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_outline_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

void jump_to_symbol(WorkspaceModel* workspace, FocusManagerState* focus,
                    const SymbolInfo& sym) {
  if (workspace == nullptr) {
    return;
  }
  workspace->record_cursor_jump();
  workspace->buffer.reset_to_single_cursor(std::max(0, sym.line - 1), 0);
  workspace->buffer.scroll = std::max(0, workspace->buffer.primary_line() - 2);
  workspace->buffer.view_token++;
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
}

bool handle_outline_scrollbar_mouse(OutlinePanelState* state, MainLayoutState* layout_state,
                                    const Mouse& m, int total, int visible) {
  if (state == nullptr || !state->scrollbar_layout.scrollable) {
    return false;
  }

  const int max_scroll = max_scroll_offset(total, visible);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kEditorScrollbar; });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (state->scrollbar_dragging) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
    return in_bar;
  }

  if (state->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    state->list_scroll = std::max(0, state->list_scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    state->list_scroll = std::min(state->list_scroll + 3, max_scroll);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kEditorScrollbar);
    const int local_y = m.y - state->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->scrollbar_layout, state->scrollbar_box, m.x, m.y)) {
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = local_y - state->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->scrollbar_layout.thumb_height / 2;
      state->list_scroll =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top),
                               max_scroll));
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = state->scrollbar_layout.thumb_height / 2;
    }
    return true;
  }

  return false;
}

bool mouse_over_outline(const OutlinePanelState* state, int x, int y) {
  if (state == nullptr) {
    return false;
  }
  return state->content_box.Contain(x, y) || state->scrollbar_box.Contain(x, y);
}

bool outline_input_blocked(const MainLayoutState* layout_state) {
  return layout_state != nullptr &&
         (is_watch_input_focus(layout_state->text_input_focus) ||
          layout_state->right_panel_active_section == 1);
}

bool handle_outline_navigation(OutlinePanelState* state, WorkspaceModel* workspace,
                               FocusManagerState* focus, MainLayoutState* layout_state,
                               Event event) {
  const int total = state->display_row_count();
  const int visible = state->last_visible_lines;
  const int max_scroll = max_scroll_offset(total, visible);

  if (event.is_mouse()) {
    const auto& m = event.mouse();
    if (handle_outline_scrollbar_mouse(state, layout_state, m, total, visible)) {
      return true;
    }
    if (m.motion == Mouse::Moved) {
      update_outline_hover(state, layout_state, m.x, m.y);
      return false;
    }
    if (m.motion == Mouse::Pressed &&
        (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) &&
        state->content_box.Contain(m.x, m.y)) {
      if (m.button == Mouse::WheelUp) {
        state->list_scroll = std::max(0, state->list_scroll - 3);
      } else {
        state->list_scroll = std::min(state->list_scroll + 3, max_scroll);
      }
      return true;
    }
  }

  if (event.is_mouse() && event.mouse().button == Mouse::Left &&
      event.mouse().motion == Mouse::Pressed) {
    const auto& m = event.mouse();
    const auto row = state->row_at_mouse(m.x, m.y);
    if (!row.has_value()) {
      return false;
    }
    if (layout_state != nullptr) {
      layout_state->right_panel_active_section = 0;
    }
    focus->region = FocusRegion::RightPanel;
    trigger_press(layout_state, press_id::outline_row(*row));
    const auto& display_row = state->display_rows[static_cast<std::size_t>(*row)];
    if (display_row.kind != OutlineRowKind::Symbol || display_row.symbol_index < 0) {
      state->selected = state->nearest_symbol_row(*row, 1);
      state->scroll_row_into_view(state->selected);
      return true;
    }
    state->selected = *row;
    state->scroll_row_into_view(*row);
    jump_to_symbol(workspace, focus,
                   state->symbols[static_cast<std::size_t>(display_row.symbol_index)]);
    return true;
  }

  if (focus->region != FocusRegion::RightPanel) {
    return false;
  }
  if (state->display_rows.empty()) {
    return false;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected = state->nearest_symbol_row(state->selected, 1);
    state->scroll_row_into_view(state->selected);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected = state->nearest_symbol_row(state->selected, -1);
    state->scroll_row_into_view(state->selected);
    return true;
  }
  if (event == Event::Return) {
    const auto& display_row = state->display_rows[static_cast<std::size_t>(state->selected)];
    if (display_row.kind != OutlineRowKind::Symbol || display_row.symbol_index < 0) {
      return true;
    }
    trigger_press(layout_state, press_id::outline_row(state->selected));
    jump_to_symbol(workspace, focus,
                   state->symbols[static_cast<std::size_t>(display_row.symbol_index)]);
    return true;
  }
  if (event == Event::PageUp) {
    state->list_scroll = std::max(0, state->list_scroll - visible);
    return true;
  }
  if (event == Event::PageDown) {
    state->list_scroll = std::min(state->list_scroll + visible, max_scroll);
    return true;
  }
  return false;
}

bool handle_outline_mouse(OutlinePanelState* state, WorkspaceModel* workspace,
                          FocusManagerState* focus, MainLayoutState* layout_state,
                          Event event) {
  if (state == nullptr || !event.is_mouse()) {
    return false;
  }
  if (outline_input_blocked(layout_state)) {
    return false;
  }
  const auto& m = event.mouse();
  if (!mouse_over_outline(state, m.x, m.y)) {
    return false;
  }
  return handle_outline_navigation(state, workspace, focus, layout_state, event);
}

}  // namespace

Component MakeOutlinePanel(WorkspaceModel* workspace, FocusManagerState* focus,
                           std::shared_ptr<ISymbolProvider> symbols,
                           MainLayoutState* layout_state) {
  auto state = std::make_shared<OutlinePanelState>();

  if (layout_state != nullptr) {
    layout_state->outline_tick_callback = [state, symbols, workspace, layout_state]() {
      const uint64_t sym_rev = symbols->document_symbols_revision();
      if (sym_rev != state->last_document_symbols_revision) {
        state->last_document_symbols_revision = sym_rev;
        if (!state->loaded_file.empty()) {
          state->symbols_fetch_pending = true;
        }
      }
      fetch_outline_symbols(state.get(), symbols.get(), workspace, layout_state);
    };
  }

  auto renderer = Renderer([workspace, focus, state, layout_state] {
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    if (path != state->loaded_file) {
      state->loaded_file = path;
      state->symbols.clear();
      state->display_rows.clear();
      state->symbols_fetch_pending = !path.empty();
      state->selected = 0;
      state->list_scroll = 0;
    }

    const int total = state->display_row_count();
    const int visible = visible_line_count(state->content_box);
    state->last_visible_lines = visible;
    state->clamp_scroll();

    const int start = state->list_scroll;
    const int end = std::min(total, start + visible);

    Elements rows;
    if (state->symbols_fetch_pending && state->symbols.empty()) {
      rows.push_back(text(i18n::tr("panel.outline.loading")) | color(theme::Muted()));
    } else if (state->symbols.empty()) {
      rows.push_back(text(i18n::tr("panel.outline.no_symbols")) | color(theme::Muted()));
    } else {
      for (int i = start; i < end; ++i) {
        const auto& row = state->display_rows[static_cast<std::size_t>(i)];
        std::string indent(static_cast<std::size_t>(row.indent), ' ');
        const bool is_scope = row.kind == OutlineRowKind::Scope;
        const std::string body = strip_symbol_kind_prefix(row.label);
        const std::string icon = symbol_kind_glyph(row.color_kind);
        const Color kind_color = theme::ColorForSymbolKind(row.color_kind);
        Element line =
            hbox({text(indent + icon + " ") | color(kind_color), text(body) | color(kind_color)});
        if (is_scope) {
          line = line | dim;
        }
        const bool selected =
            i == state->selected && focus->region == FocusRegion::RightPanel &&
            !is_scope;
        const std::string row_id = press_id::outline_row(i);
        const bool hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
        const bool pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
        line = StyleListRow(std::move(line), selected, hovered, pressed);
        rows.push_back(std::move(line));
      }
    }

    const int rendered_lines = std::max(1, static_cast<int>(rows.size()));
    state->scrollbar_layout =
        compute_scrollbar_layout(total, state->list_scroll, visible, rendered_lines);

    Element list = vbox(std::move(rows)) | reflect(state->content_box) | flex |
                   bgcolor(theme::PanelBg());
    Element scrollbar =
        vertical_scrollbar(total, state->list_scroll, visible, rendered_lines,
                           layout_state != nullptr &&
                               layout_state->clickable.is_hovered(press_id::kEditorScrollbar),
                           state->scrollbar_dragging ||
                               (layout_state != nullptr &&
                                layout_state->clickable.is_pressed(press_id::kEditorScrollbar))) |
        reflect(state->scrollbar_box);

    return PanelBody(hbox({list | flex, scrollbar}) | flex);
  });

  if (layout_state != nullptr) {
    layout_state->outline_mouse_handler =
        [state, workspace, focus, layout_state](const Event& event) {
          return handle_outline_mouse(state.get(), workspace, focus, layout_state, event);
        };
  }

  return WrapFocusable(CatchEvent(renderer, [workspace, focus, state, layout_state](Event event) {
    if (event.is_mouse() &&
        mouse_over_outline(state.get(), event.mouse().x, event.mouse().y)) {
      return false;
    }
    if (outline_input_blocked(layout_state)) {
      return false;
    }
    return handle_outline_navigation(state.get(), workspace, focus, layout_state, event);
  }));
}

}  // namespace tgdb
