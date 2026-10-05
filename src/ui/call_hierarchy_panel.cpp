#include "ui/call_hierarchy_panel.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/node.hpp"
#include "ftxui/screen/box.hpp"
#include "ftxui/screen/screen.hpp"
#include "ftxui/util/autoreset.hpp"
#include "ui/call_hierarchy_view.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/search_panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tuide {

using namespace ftxui;

namespace {

struct SegmentSpan {
  int node_index = -1;
  Box box;
};

struct RowLayout {
  Box box;
  int visible_row = 0;
  int leaf_node = -1;
  Box location_box;
  std::vector<SegmentSpan> segments;
};

struct HierarchyHit {
  int visible_row = 0;
  int node_index = -1;
};

struct CallHierarchyPanelState {
  Box results_box;
  std::vector<RowLayout> row_layouts;
  int scroll_line = 0;
};

// frame() only follows the focused element. Wheel events never reach it through
// the console mouse dispatcher, so the list scrolls from this offset instead.
class YScroll : public Node {
 public:
  YScroll(Elements children, int* scroll) : Node(std::move(children)), scroll_(scroll) {}

  void SetBox(Box box) override {
    Node::SetBox(box);
    const int external_y = box.y_max - box.y_min;
    const int internal_y = std::max(requirement_.min_y, external_y);
    const int max_dy = std::max(0, internal_y - external_y - 1);
    int dy = 0;
    if (scroll_ != nullptr) {
      dy = std::max(0, std::min(*scroll_, max_dy));
      *scroll_ = dy;
    }
    const int external_x = box.x_max - box.x_min;
    const int internal_x = std::max(requirement_.min_x, external_x);
    Box child = box;
    child.x_min = box.x_min;
    child.x_max = box.x_min + internal_x;
    child.y_min = box.y_min - dy;
    child.y_max = box.y_min + internal_y - dy;
    children_[0]->SetBox(child);
  }

  void Render(Screen& screen) override {
    const AutoReset<Box> stencil(&screen.stencil, Box::Intersection(box_, screen.stencil));
    children_[0]->Render(screen);
  }

 private:
  int* scroll_ = nullptr;
};

ElementDecorator y_scroll_to(int* scroll) {
  return [scroll](Element child) {
    Elements children;
    children.push_back(std::move(child));
    return std::make_shared<YScroll>(std::move(children), scroll);
  };
}

void reveal_hierarchy_row(CallHierarchyPanelState* state, int selected, int row_count) {
  if (state == nullptr || state->results_box.IsEmpty() || row_count <= 0) {
    return;
  }
  selected = std::max(0, std::min(selected, row_count - 1));
  if (selected >= static_cast<int>(state->row_layouts.size())) {
    return;
  }
  const Box& box = state->row_layouts[static_cast<std::size_t>(selected)].box;
  if (box.IsEmpty()) {
    return;
  }
  if (box.y_min < state->results_box.y_min) {
    state->scroll_line -= state->results_box.y_min - box.y_min;
  } else if (box.y_max > state->results_box.y_max) {
    state->scroll_line += box.y_max - state->results_box.y_max;
  }
  if (state->scroll_line < 0) {
    state->scroll_line = 0;
  }
}

std::string chain_segment_label(const CallHierarchyViewState& hierarchy, int node_index) {
  const auto& node = hierarchy.nodes[static_cast<std::size_t>(node_index)];
  std::string label = node.item.name;
  if (!node.item.detail.empty()) {
    label += " (" + node.item.detail + ")";
  }
  return label;
}

int hit_node_in_row(const RowLayout& row, int x, int y) {
  if (row.segments.empty()) {
    return row.leaf_node;
  }
  for (const SegmentSpan& seg : row.segments) {
    if (!seg.box.IsEmpty() && seg.box.Contain(x, y)) {
      return seg.node_index;
    }
  }
  if (!row.location_box.IsEmpty() && row.location_box.Contain(x, y)) {
    return row.leaf_node;
  }

  // Clic en flechas / huecos: el segmento más cercano a la izquierda.
  int node_index = row.segments.front().node_index;
  for (const SegmentSpan& seg : row.segments) {
    if (!seg.box.IsEmpty() && x >= seg.box.x_min) {
      node_index = seg.node_index;
    }
  }
  return node_index;
}

std::optional<int> visible_row_at_mouse(const CallHierarchyPanelState& state, int x, int y) {
  bool laid_out = false;
  for (std::size_t i = 0; i < state.row_layouts.size(); ++i) {
    const Box& box = state.row_layouts[i].box;
    if (box.IsEmpty()) {
      continue;
    }
    laid_out = true;
    if (box.Contain(x, y)) {
      return static_cast<int>(i);
    }
  }
  if (laid_out || !state.results_box.Contain(x, y)) {
    return std::nullopt;
  }
  const int row = y - state.results_box.y_min;
  if (row < 0 || row >= static_cast<int>(state.row_layouts.size())) {
    return std::nullopt;
  }
  return row;
}

int display_cols(const std::string& text) {
  int cols = 0;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t len = 1;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > text.size()) {
      len = 1;
    }
    i += len;
    ++cols;
  }
  return cols;
}

std::vector<std::string> wrap_columns(const std::string& text, int width) {
  std::vector<std::string> lines;
  if (width < 1) {
    width = 1;
  }
  std::string line;
  int cols = 0;
  std::string word;
  int word_cols = 0;
  auto push_line = [&]() {
    lines.push_back(line);
    line.clear();
    cols = 0;
  };
  auto flush_word = [&]() {
    if (word.empty()) {
      return;
    }
    if (word_cols > width) {
      std::size_t i = 0;
      while (i < word.size()) {
        const unsigned char c = static_cast<unsigned char>(word[i]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) {
          len = 2;
        } else if ((c & 0xF0) == 0xE0) {
          len = 3;
        } else if ((c & 0xF8) == 0xF0) {
          len = 4;
        }
        if (i + len > word.size()) {
          len = 1;
        }
        if (cols >= width) {
          push_line();
        }
        line.append(word, i, len);
        i += len;
        ++cols;
      }
      word.clear();
      word_cols = 0;
      return;
    }
    if (cols > 0 && cols + word_cols > width) {
      push_line();
    }
    line += word;
    cols += word_cols;
    word.clear();
    word_cols = 0;
  };
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t len = 1;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
    }
    if (i + len > text.size()) {
      len = 1;
    }
    const std::string ch = text.substr(i, len);
    i += len;
    if (ch == " " || ch == "\t" || ch == "\n") {
      flush_word();
      if (cols > 0 && cols + 1 > width) {
        push_line();
      } else if (cols > 0 && ch != "\n") {
        line.push_back(' ');
        ++cols;
      } else if (ch == "\n" && !line.empty()) {
        push_line();
      }
      continue;
    }
    word += ch;
    ++word_cols;
  }
  flush_word();
  if (line.empty() && lines.empty()) {
    lines.emplace_back();
  } else if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

Element fitted_text(const std::string& prefix, const std::string& body, int max_width, Color ink) {
  const int prefix_cols = display_cols(prefix);
  const int budget = std::max(8, max_width - prefix_cols);
  const std::vector<std::string> lines = wrap_columns(body, budget);
  if (lines.size() <= 1) {
    return text(prefix + (lines.empty() ? std::string{} : lines.front())) | color(ink);
  }
  const std::string pad(static_cast<std::size_t>(prefix_cols), ' ');
  Elements stacked;
  stacked.reserve(lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    stacked.push_back(text((i == 0 ? prefix : pad) + lines[i]) | color(ink));
  }
  return vbox(std::move(stacked));
}

std::optional<HierarchyHit> hierarchy_hit_at_mouse(const CallHierarchyPanelState& state, int x,
                                                 int y) {
  const auto visible_row = visible_row_at_mouse(state, x, y);
  if (!visible_row.has_value()) {
    return std::nullopt;
  }
  const RowLayout& row = state.row_layouts[static_cast<std::size_t>(*visible_row)];
  return HierarchyHit{*visible_row, hit_node_in_row(row, x, y)};
}

bool update_call_hierarchy_hover(CallHierarchyPanelState* state, MainLayoutState* layout_state,
                                 int x, int y) {
  if (!hover_effects_enabled()) {
    return false;
  }
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  if (const auto hit = hierarchy_hit_at_mouse(*state, x, y)) {
    layout_state->clickable.set_hover(press_id::call_hierarchy_seg(hit->node_index));
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_call_hierarchy_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    wake_console_panel(layout_state);
    return true;
  }
  return false;
}

Element render_chain_row(const CallHierarchyViewState& hierarchy, int visible_row,
                         int node_index, bool selected, MainLayoutState* layout_state,
                         RowLayout* layout, int max_width, bool conditions_only) {
  const CallHierarchyTreeNode& leaf =
      hierarchy.nodes[static_cast<std::size_t>(node_index)];
  std::vector<int> chain = call_hierarchy_chain_indices(hierarchy, node_index);
  const bool causal = hierarchy.kind == CallHierarchyContentKind::CausalFlow;
  if (causal && !chain.empty() && chain.front() == 0) {
    chain.erase(chain.begin());
  }
  if (chain.empty()) {
    chain.push_back(node_index);
  }
  std::string conditions;
  if (conditions_only) {
    for (int index : chain) {
      if (index < 0 || index >= static_cast<int>(hierarchy.nodes.size())) {
        continue;
      }
      const std::string& detail = hierarchy.nodes[static_cast<std::size_t>(index)].item.detail;
      if (detail.empty()) {
        continue;
      }
      if (!conditions.empty()) {
        conditions += " · ";
      }
      conditions += detail;
    }
    if (conditions.empty()) {
      conditions = i18n::tr("panel.causal_flow.no_condition");
    }
    chain.assign(1, node_index);
  }

  layout->visible_row = visible_row;
  layout->leaf_node = node_index;
  layout->segments.assign(chain.size(), SegmentSpan{});

  struct Piece {
    std::string text;
    Element element;
    int cols = 0;
  };
  std::vector<Piece> pieces;
  pieces.reserve(chain.size() + 2);
  for (std::size_t s = 0; s < chain.size(); ++s) {
    const int seg_index = chain[s];
    SegmentSpan& span = layout->segments[s];
    span.node_index = seg_index;
    const std::string label =
        conditions_only ? conditions : chain_segment_label(hierarchy, seg_index);
    const std::string shown = (s == 0 ? std::string{} : std::string(" -> ")) + label;
    const std::string seg_id = press_id::call_hierarchy_seg(seg_index);
    const bool hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(seg_id);
    const bool pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(seg_id);
    const auto& seg_node = hierarchy.nodes[static_cast<std::size_t>(seg_index)];
    Element segment =
        text(shown) | color(theme::ColorForSymbolKind(seg_node.item.kind));
    segment = StyleClickable(std::move(segment), {false, hovered, pressed, false});
    pieces.push_back(Piece{shown, std::move(segment) | reflect(span.box), display_cols(shown)});
  }

  const std::string location = "  @ " + call_hierarchy_node_location(leaf);
  pieces.push_back(Piece{location,
                         text(location) | color(theme::Muted()) | reflect(layout->location_box),
                         display_cols(location)});
  if (causal && !conditions_only && !leaf.preview.empty()) {
    const std::string preview = "  " + leaf.preview;
    pieces.push_back(
        Piece{preview, text(preview) | color(theme::Header()), display_cols(preview)});
  }

  int total = 1;
  for (const Piece& piece : pieces) {
    total += piece.cols;
  }
  const bool wrap = causal && max_width >= 16 && total > max_width;

  Element row;
  if (!wrap) {
    Elements parts;
    parts.push_back(text(" "));
    for (Piece& piece : pieces) {
      parts.push_back(std::move(piece.element));
    }
    row = hbox(std::move(parts));
  } else {
    Elements lines;
    Elements current;
    int used = 1;
    current.push_back(text(" "));
    auto flush = [&]() {
      if (current.size() <= 1 && lines.empty()) {
        return;
      }
      lines.push_back(hbox(std::move(current)));
      current.clear();
      current.push_back(text("  "));
      used = 2;
    };
    for (std::size_t i = 0; i < pieces.size(); ++i) {
      Piece& piece = pieces[i];
      if (used > 2 && used + piece.cols > max_width) {
        flush();
      }
      if (piece.cols > std::max(8, max_width - used)) {
        if (current.size() > 1) {
          flush();
        }
        std::string lead = used == 1 ? " " : "  ";
        std::string body = piece.text;
        Color piece_color = theme::Muted();
        if (i < chain.size()) {
          piece_color = theme::ColorForSymbolKind(
              hierarchy.nodes[static_cast<std::size_t>(chain[i])].item.kind);
          if (body.rfind(" -> ", 0) == 0) {
            body.erase(0, 4);
            lead += "-> ";
          }
        } else if (body.rfind("  @ ", 0) == 0) {
          body.erase(0, 4);
          lead += "@ ";
        } else if (body.rfind("  ", 0) == 0) {
          body.erase(0, 2);
          piece_color = theme::Header();
        }
        Element wrapped = fitted_text(lead, body, max_width, piece_color);
        if (i < chain.size()) {
          const std::string seg_id = press_id::call_hierarchy_seg(chain[i]);
          const bool hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(seg_id);
          const bool pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(seg_id);
          wrapped = StyleClickable(std::move(wrapped), {false, hovered, pressed, false});
          wrapped = std::move(wrapped) | reflect(layout->segments[i].box);
        } else if (piece.text.rfind("  @ ", 0) == 0) {
          wrapped = std::move(wrapped) | reflect(layout->location_box);
        }
        if (current.size() > 1) {
          flush();
        }
        lines.push_back(std::move(wrapped));
        current.clear();
        current.push_back(text("  "));
        used = 2;
        continue;
      }
      current.push_back(std::move(piece.element));
      used += piece.cols;
    }
    if (current.size() > 1) {
      lines.push_back(hbox(std::move(current)));
    }
    row = lines.size() == 1 ? std::move(lines.front()) : vbox(std::move(lines));
  }
  if (selected) {
    row = row | bgcolor(theme::TabIdle());
  }
  return row | reflect(layout->box);
}

Element render_causal_tree_row(const CallHierarchyViewState& hierarchy, int visible_row,
                              int node_index, bool selected, MainLayoutState* layout_state,
                              RowLayout* layout, int max_width, bool conditions_only) {
  const CallHierarchyTreeNode& node = hierarchy.nodes[static_cast<std::size_t>(node_index)];
  layout->visible_row = visible_row;
  layout->leaf_node = node_index;
  layout->segments.assign(1, SegmentSpan{});
  layout->segments.front().node_index = node_index;

  const int indent_n = std::max(0, node.depth - 1);
  const std::string indent(static_cast<std::size_t>(indent_n) * 2, ' ');
  std::string name = node.item.name.empty() ? std::string("?") : node.item.name;
  if (conditions_only) {
    name = node.item.detail.empty() ? i18n::tr("panel.causal_flow.no_condition") : node.item.detail;
  }
  const std::string prefix = " " + indent;
  const std::string seg_id = press_id::call_hierarchy_seg(node_index);
  const bool hovered = layout_state != nullptr && layout_state->clickable.is_hovered(seg_id);
  const bool pressed = layout_state != nullptr && layout_state->clickable.is_pressed(seg_id);
  const std::string location = "  @ " + call_hierarchy_node_location(node);
  const bool show_detail = !conditions_only && !node.item.detail.empty() &&
                           name.find(node.item.detail) == std::string::npos;
  const bool show_preview = !conditions_only && !node.preview.empty() &&
                            name.find(node.preview) == std::string::npos;
  const int width = std::max(16, max_width);
  int cols = display_cols(prefix + name + location);
  if (show_detail) {
    cols += display_cols(node.item.detail) + 2;
  }
  if (show_preview) {
    cols += display_cols(node.preview) + 2;
  }

  Element label = fitted_text(prefix, name, width, theme::ColorForSymbolKind(node.item.kind));
  label = StyleClickable(std::move(label), {false, hovered, pressed, false});
  label = std::move(label) | reflect(layout->segments.front().box);

  Element row;
  if (cols <= width) {
    Elements parts;
    parts.push_back(std::move(label));
    if (show_detail) {
      parts.push_back(text("  " + node.item.detail) | color(theme::Muted()));
    }
    parts.push_back(text(location) | color(theme::Muted()) | reflect(layout->location_box));
    if (show_preview) {
      parts.push_back(text("  " + node.preview) | color(theme::Header()));
    }
    row = hbox(std::move(parts));
  } else {
    Elements lines;
    lines.push_back(std::move(label));
    Elements tail;
    tail.push_back(text(prefix));
    if (show_detail) {
      tail.push_back(text("  " + node.item.detail) | color(theme::Muted()));
    }
    tail.push_back(text(location) | color(theme::Muted()) | reflect(layout->location_box));
    lines.push_back(hbox(std::move(tail)));
    if (show_preview) {
      lines.push_back(fitted_text(prefix + "  ", node.preview, width, theme::Header()));
    }
    row = vbox(std::move(lines));
  }
  if (selected) {
    row = row | bgcolor(theme::TabIdle());
  }
  return row | reflect(layout->box);
}

Element render_reference_row(const CallHierarchyViewState& hierarchy, int visible_row,
                             int node_index, bool selected, MainLayoutState* layout_state,
                             RowLayout* layout) {
  const CallHierarchyTreeNode& node =
      hierarchy.nodes[static_cast<std::size_t>(node_index)];

  layout->visible_row = visible_row;
  layout->leaf_node = node_index;
  layout->segments.assign(1, SegmentSpan{});
  layout->segments.front().node_index = node_index;
  layout->location_box = Box{};

  const std::string seg_id = press_id::call_hierarchy_seg(node_index);
  const bool hovered =
      layout_state != nullptr && layout_state->clickable.is_hovered(seg_id);
  const bool pressed =
      layout_state != nullptr && layout_state->clickable.is_pressed(seg_id);

  const std::string location = call_hierarchy_node_location(node);
  Element label = text(" " + location) | color(theme::ColorForSymbolKind(node.item.kind));
  label = StyleClickable(std::move(label), {false, hovered, pressed, false});

  Elements parts;
  parts.push_back(std::move(label) | reflect(layout->segments.front().box));
  if (!node.preview.empty()) {
    parts.push_back(text("  "));
    parts.push_back(text(node.preview) | color(theme::Header()));
  }

  Element row = hbox(std::move(parts));
  if (selected) {
    row = row | bgcolor(theme::TabIdle());
  }
  return row | reflect(layout->box);
}

}  // namespace

Component MakeCallHierarchyPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                 MainLayoutState* layout_state, RightSidebarState* sidebar,
                                 const std::shared_ptr<ISymbolProvider>& symbols,
                                 WorkspaceIndexer* indexer) {
  auto state = std::make_shared<CallHierarchyPanelState>();

  auto handler = [state, workspace, focus, layout_state, sidebar, symbols, indexer](Event event) {
    if (sidebar == nullptr) {
      return false;
    }

    if (event == Event::Custom && sidebar->pending_call_hierarchy) {
      sidebar->pending_call_hierarchy = false;
      const int line = sidebar->pending_call_hierarchy_line;
      const int col = sidebar->pending_call_hierarchy_col;
      const std::string symbol = sidebar->pending_call_hierarchy_symbol;
      sidebar->pending_call_hierarchy_symbol.clear();
      state->scroll_line = 0;
      open_call_hierarchy_view(&sidebar->call_hierarchy, workspace, layout_state, sidebar, symbols,
                               line, col, symbol);
      clear_search_input_focus(layout_state);
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      return true;
    }

    if (event == Event::Custom && sidebar->pending_causal_connect) {
      sidebar->pending_causal_connect = false;
      const std::string symbol = sidebar->pending_causal_connect_symbol;
      sidebar->pending_causal_connect_symbol.clear();
      state->scroll_line = 0;
      connect_causal_flow_view(&sidebar->call_hierarchy, workspace, symbol);
      clear_search_input_focus(layout_state);
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      wake_console_panel(layout_state);
      return true;
    }

    if (event == Event::Custom && sidebar->pending_causal_flow) {
      sidebar->pending_causal_flow = false;
      const std::string symbol = sidebar->pending_causal_flow_symbol;
      const int line = sidebar->pending_causal_flow_line;
      const std::string path = sidebar->pending_causal_flow_path;
      sidebar->pending_causal_flow_symbol.clear();
      sidebar->pending_causal_flow_line = -1;
      sidebar->pending_causal_flow_path.clear();
      state->scroll_line = 0;
      open_causal_flow_view(&sidebar->call_hierarchy, workspace, layout_state, sidebar, symbol,
                            indexer, line, path);
      clear_search_input_focus(layout_state);
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      return true;
    }

    if (event == Event::Custom && sidebar->pending_references) {
      sidebar->pending_references = false;
      const int line = sidebar->pending_references_line;
      const int col = sidebar->pending_references_col;
      const std::string symbol = sidebar->pending_references_symbol;
      sidebar->pending_references_symbol.clear();
      state->scroll_line = 0;
      open_references_view(&sidebar->call_hierarchy, workspace, layout_state, sidebar, symbols, line,
                           col, symbol);
      clear_search_input_focus(layout_state);
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      return true;
    }

    if (!call_hierarchy_tab_active(layout_state)) {
      return false;
    }

    CallHierarchyViewState* hierarchy = &sidebar->call_hierarchy;
    const bool active = hierarchy->active;

    if (active) {
      const std::vector<int> visible = call_hierarchy_visible_rows(*hierarchy);
      auto clamp_selection = [&]() {
        if (visible.empty()) {
          hierarchy->selected = 0;
          return;
        }
        hierarchy->selected =
            std::max(0, std::min(hierarchy->selected, static_cast<int>(visible.size()) - 1));
      };
      clamp_selection();

      if (event.is_mouse()) {
        const auto& m = event.mouse();
        if (m.motion == Mouse::Moved) {
          update_call_hierarchy_hover(state.get(), layout_state, m.x, m.y);
          return false;
        }
        if ((m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) &&
            (state->results_box.IsEmpty() || state->results_box.Contain(m.x, m.y))) {
          const int delta = m.button == Mouse::WheelUp ? -3 : 3;
          state->scroll_line = std::max(0, state->scroll_line + delta);
          wake_console_panel(layout_state);
          return true;
        }
      }

      if (hierarchy->kind == CallHierarchyContentKind::CausalFlow) {
        if (event == Event::Character('a')) {
          toggle_causal_point_view(hierarchy, workspace, layout_state, sidebar, indexer);
          state->scroll_line = 0;
          return true;
        }
        if (event == Event::Character('u')) {
          hierarchy->causal_fold = hierarchy->causal_fold == CausalFlowFold::Upstream
                                       ? CausalFlowFold::All
                                       : CausalFlowFold::Upstream;
          hierarchy->selected = 0;
          state->scroll_line = 0;
          return true;
        }
        if (event == Event::Character('d')) {
          hierarchy->causal_fold = hierarchy->causal_fold == CausalFlowFold::Downstream
                                       ? CausalFlowFold::All
                                       : CausalFlowFold::Downstream;
          hierarchy->selected = 0;
          state->scroll_line = 0;
          return true;
        }
        if (event == Event::Character('c')) {
          hierarchy->causal_conditions = !hierarchy->causal_conditions;
          return true;
        }
      }

      if (event == Event::Escape) {
        hierarchy->clear();
        return true;
      }
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!visible.empty()) {
          hierarchy->selected =
              std::min(hierarchy->selected + 1, static_cast<int>(visible.size()) - 1);
          reveal_hierarchy_row(state.get(), hierarchy->selected, static_cast<int>(visible.size()));
        }
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        hierarchy->selected = std::max(0, hierarchy->selected - 1);
        reveal_hierarchy_row(state.get(), hierarchy->selected, static_cast<int>(visible.size()));
        return true;
      }
      if (event == Event::Return) {
        if (!visible.empty()) {
          const int node_index = visible[static_cast<std::size_t>(hierarchy->selected)];
          trigger_press(layout_state, press_id::call_hierarchy_seg(node_index));
          navigate_to_call_hierarchy_node(
              workspace, focus, layout_state,
              hierarchy->nodes[static_cast<std::size_t>(node_index)]);
        }
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (const auto hit = hierarchy_hit_at_mouse(*state, m.x, m.y)) {
          if (focus != nullptr) {
            focus->region = FocusRegion::Terminal;
          }
          if (layout_state != nullptr) {
            layout_state->right_panel_active_section = 0;
          }
          hierarchy->selected = hit->visible_row;
          trigger_press(layout_state, press_id::call_hierarchy_seg(hit->node_index));
          navigate_to_call_hierarchy_node(
              workspace, focus, layout_state,
              hierarchy->nodes[static_cast<std::size_t>(hit->node_index)]);
          return true;
        }
      }
      return false;
    }

    if (focus != nullptr && focus->region == FocusRegion::Terminal) {
      return event == Event::Escape;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->call_hierarchy_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(
      Renderer([state, sidebar, focus, layout_state] {
        CallHierarchyViewState* hierarchy =
            sidebar != nullptr ? &sidebar->call_hierarchy : nullptr;
        const bool active = hierarchy != nullptr && hierarchy->active;

        Element header;
        Elements rows;

        if (!active) {
          state->row_layouts.clear();
          rows.push_back(text(i18n::tr("panel.call_hierarchy.inactive")) |
                         color(theme::Muted()));
        } else {
          const bool is_references =
              hierarchy->kind == CallHierarchyContentKind::References;
          const bool is_causal = hierarchy->kind == CallHierarchyContentKind::CausalFlow;
          const Color root_color =
              is_references ? theme::SyntaxFunction()
                            : (hierarchy->nodes.empty()
                                   ? theme::SyntaxFunction()
                                   : theme::ColorForSymbolKind(hierarchy->nodes.front().item.kind));
          const std::string header_label =
              is_references ? (i18n::tr("panel.references.title") + ": " + hierarchy->root_label)
              : is_causal   ? (i18n::tr("panel.causal_flow.title") + ": " + hierarchy->root_label)
                            : hierarchy->root_label;
          const std::string footer_key = is_references ? "panel.references.footer"
                                         : is_causal   ? "panel.causal_flow.footer"
                                                       : "panel.call_hierarchy.footer";
          Element footer = text(" " + i18n::tr_fmt(footer_key, {hierarchy->status})) |
                           color(theme::Muted());
          if (is_causal) {
            footer = paragraphAlignLeft(" " + i18n::tr_fmt(footer_key, {hierarchy->status})) |
                     color(theme::Muted()) | size(HEIGHT, LESS_THAN, 4);
          } else {
            footer = std::move(footer) | size(HEIGHT, EQUAL, 1);
          }
          header = vbox({
              text(" " + header_label) | color(root_color) | bold,
              separator(),
              std::move(footer),
          });

          const std::vector<int> visible = call_hierarchy_visible_rows(*hierarchy);
          if (visible.empty()) {
            state->row_layouts.clear();
            rows.push_back(text(i18n::tr("common.no_results")) | color(theme::Muted()));
          } else {
            // Preasignar layouts: reflect guarda referencias a Box que deben
            // permanecer válidas durante el layout/render de FTXUI.
            state->row_layouts.assign(static_cast<std::size_t>(visible.size()), RowLayout{});
            int causal_width = 0;
            if (is_causal) {
              if (!state->results_box.IsEmpty()) {
                causal_width = state->results_box.x_max - state->results_box.x_min + 1;
              }
              if (causal_width < 20) {
                causal_width = layout_state != nullptr
                                   ? std::max(40, terminal_width_or_default(layout_state->terminal_width) - 2)
                                   : 80;
              }
            }
            auto push_chain = [&](int i, int node_index) {
              const bool selected = i == hierarchy->selected && focus != nullptr &&
                                    focus->region == FocusRegion::Terminal;
              if (is_references) {
                rows.push_back(render_reference_row(*hierarchy, i, node_index, selected, layout_state,
                                                    &state->row_layouts[static_cast<std::size_t>(i)]));
                return;
              }
              if (is_causal) {
                rows.push_back(render_causal_tree_row(
                    *hierarchy, i, node_index, selected, layout_state,
                    &state->row_layouts[static_cast<std::size_t>(i)], causal_width,
                    hierarchy->causal_conditions && selected));
                return;
              }
              rows.push_back(render_chain_row(
                  *hierarchy, i, node_index, selected, layout_state,
                  &state->row_layouts[static_cast<std::size_t>(i)], causal_width, false));
            };
            if (!is_causal) {
              for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
                push_chain(i, visible[static_cast<std::size_t>(i)]);
              }
            } else {
              std::vector<int> upstream;
              std::vector<int> downstream;
              for (int node_index : visible) {
                const CausalFlowSide side =
                    hierarchy->nodes[static_cast<std::size_t>(node_index)].causal_side;
                if (side == CausalFlowSide::Downstream) {
                  downstream.push_back(node_index);
                } else {
                  upstream.push_back(node_index);
                }
              }
              auto emit_section = [&](const char* title_key, Color ink,
                                      const std::vector<int>& group, int* slot) {
                rows.push_back(text(" " + i18n::tr(title_key)) | bold | color(ink));
                if (group.empty()) {
                  rows.push_back(text("  " + i18n::tr("panel.causal_flow.empty_section")) |
                                 color(theme::Muted()));
                  return;
                }
                for (int node_index : group) {
                  push_chain(*slot, node_index);
                  ++(*slot);
                }
              };
              int slot = 0;
              if (hierarchy->causal_fold != CausalFlowFold::Downstream) {
                emit_section("panel.causal_flow.upstream", theme::SyntaxFunction(), upstream, &slot);
              }
              if (hierarchy->causal_fold != CausalFlowFold::Upstream) {
                emit_section("panel.causal_flow.downstream", theme::SyntaxType(), downstream, &slot);
              }
            }
          }
        }

        auto results = vbox(std::move(rows)) | vscroll_indicator | y_scroll_to(&state->scroll_line) |
                       flex | reflect(state->results_box) | bgcolor(theme::PanelBg());
        if (!active) {
          return PanelBody(std::move(results));
        }
        return PanelBody(vbox({std::move(header), std::move(results)}));
      }),
      handler));
}

}  // namespace tuide
