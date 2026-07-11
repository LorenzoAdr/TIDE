#include "ui/call_hierarchy_panel.hpp"
#include "ui/ui_wake.hpp"

#include <string>
#include <string_view>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/call_hierarchy_view.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/search_panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct SegmentSpan {
  int node_index = -1;
  int x_min = 0;
  int x_max = 0;
};

struct RowLayout {
  Box box;
  int visible_row = 0;
  int leaf_node = -1;
  int location_x_min = 0;
  std::vector<SegmentSpan> segments;
};

struct HierarchyHit {
  int visible_row = 0;
  int node_index = -1;
};

struct CallHierarchyPanelState {
  Box results_box;
  std::vector<RowLayout> row_layouts;
};

std::string chain_segment_label(const CallHierarchyViewState& hierarchy, int node_index) {
  const auto& node = hierarchy.nodes[static_cast<std::size_t>(node_index)];
  std::string label = node.item.name;
  if (!node.item.detail.empty()) {
    label += " (" + node.item.detail + ")";
  }
  return label;
}

int local_x_in_row(const CallHierarchyPanelState& state, const RowLayout& row, int mouse_x) {
  if (!row.box.IsEmpty()) {
    return mouse_x - row.box.x_min;
  }
  int base_x = state.results_box.x_min;
  if (!state.results_box.IsEmpty()) {
    base_x += 1;
  }
  return mouse_x - base_x;
}

int hit_node_in_row(const RowLayout& row, int local_x) {
  if (row.segments.empty()) {
    return row.leaf_node;
  }
  for (const SegmentSpan& seg : row.segments) {
    if (local_x >= seg.x_min && local_x < seg.x_max) {
      return seg.node_index;
    }
  }
  int node_index = row.segments.front().node_index;
  for (const SegmentSpan& seg : row.segments) {
    if (local_x >= seg.x_min && local_x < row.location_x_min) {
      node_index = seg.node_index;
    }
  }
  if (row.location_x_min > 0 && local_x >= row.location_x_min) {
    return row.leaf_node;
  }
  return node_index;
}

std::optional<int> visible_row_at_mouse(const CallHierarchyPanelState& state, int x, int y) {
  for (std::size_t i = 0; i < state.row_layouts.size(); ++i) {
    const Box& box = state.row_layouts[i].box;
    if (!box.IsEmpty() && box.Contain(x, y)) {
      return static_cast<int>(i);
    }
  }
  if (!state.results_box.Contain(x, y)) {
    return std::nullopt;
  }
  const int row = y - state.results_box.y_min;
  if (row < 0 || row >= static_cast<int>(state.row_layouts.size())) {
    return std::nullopt;
  }
  return row;
}

std::optional<HierarchyHit> hierarchy_hit_at_mouse(const CallHierarchyPanelState& state, int x,
                                                 int y) {
  const auto visible_row = visible_row_at_mouse(state, x, y);
  if (!visible_row.has_value()) {
    return std::nullopt;
  }
  const RowLayout& row = state.row_layouts[static_cast<std::size_t>(*visible_row)];
  const int local_x = local_x_in_row(state, row, x);
  return HierarchyHit{*visible_row, hit_node_in_row(row, local_x)};
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
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

Element render_chain_row(const CallHierarchyViewState& hierarchy, int visible_row,
                         int node_index, bool selected, MainLayoutState* layout_state,
                         RowLayout* layout) {
  const CallHierarchyTreeNode& leaf =
      hierarchy.nodes[static_cast<std::size_t>(node_index)];
  const std::vector<int> chain = call_hierarchy_chain_indices(hierarchy, node_index);

  layout->visible_row = visible_row;
  layout->leaf_node = node_index;
  layout->segments.clear();

  Elements parts;
  parts.push_back(text(" "));
  int x = 1;
  for (std::size_t s = 0; s < chain.size(); ++s) {
    const int seg_index = chain[s];
    const std::string label = chain_segment_label(hierarchy, seg_index);
    SegmentSpan span;
    span.node_index = seg_index;
    span.x_min = x;
    span.x_max = x + static_cast<int>(label.size());
    layout->segments.push_back(span);
    x = span.x_max;

    const std::string seg_id = press_id::call_hierarchy_seg(seg_index);
    const bool hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(seg_id);
    const bool pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(seg_id);
    const auto& seg_node = hierarchy.nodes[static_cast<std::size_t>(seg_index)];
    Element segment = text(label) | color(theme::ColorForSymbolKind(seg_node.item.kind));
    segment = StyleClickable(std::move(segment), {false, hovered, pressed, false});
    parts.push_back(std::move(segment));

    if (s + 1 < chain.size()) {
      constexpr std::string_view kArrow = " -> ";
      x += static_cast<int>(kArrow.size());
      parts.push_back(text(std::string(kArrow)) | color(theme::Muted()));
    }
  }

  parts.push_back(text("  @ " + call_hierarchy_node_location(leaf)) | color(theme::Muted()));
  layout->location_x_min = x;

  Element row = hbox(std::move(parts));
  if (selected) {
    row = row | bgcolor(theme::TabIdle());
  }
  return row | reflect(layout->box);
}

}  // namespace

Component MakeCallHierarchyPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                 MainLayoutState* layout_state, RightSidebarState* sidebar,
                                 const std::shared_ptr<ISymbolProvider>& symbols) {
  auto state = std::make_shared<CallHierarchyPanelState>();

  auto handler = [state, workspace, focus, layout_state, sidebar, symbols](Event event) {
    if (sidebar == nullptr) {
      return false;
    }

    if (event == Event::Custom && sidebar->pending_call_hierarchy) {
      sidebar->pending_call_hierarchy = false;
      const int line = sidebar->pending_call_hierarchy_line;
      const int col = sidebar->pending_call_hierarchy_col;
      const std::string symbol = sidebar->pending_call_hierarchy_symbol;
      sidebar->pending_call_hierarchy_symbol.clear();
      open_call_hierarchy_view(&sidebar->call_hierarchy, workspace, layout_state, sidebar, symbols,
                               line, col, symbol);
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
      }

      if (event == Event::Escape) {
        hierarchy->clear();
        return true;
      }
      if (event == Event::ArrowDown || event == Event::Character('j')) {
        if (!visible.empty()) {
          hierarchy->selected =
              std::min(hierarchy->selected + 1, static_cast<int>(visible.size()) - 1);
        }
        return true;
      }
      if (event == Event::ArrowUp || event == Event::Character('k')) {
        hierarchy->selected = std::max(0, hierarchy->selected - 1);
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
        state->row_layouts.clear();

        if (!active) {
          rows.push_back(text(i18n::tr("panel.call_hierarchy.inactive")) |
                         color(theme::Muted()));
        } else {
          const Color root_color = hierarchy->nodes.empty()
                                       ? theme::SyntaxFunction()
                                       : theme::ColorForSymbolKind(hierarchy->nodes.front().item.kind);
          header = vbox({
              text(" " + hierarchy->root_label) | color(root_color) | bold,
              separator(),
              text(" " + i18n::tr_fmt("panel.call_hierarchy.footer", {hierarchy->status})) |
                  color(theme::Muted()) | size(HEIGHT, EQUAL, 1),
          });

          const std::vector<int> visible = call_hierarchy_visible_rows(*hierarchy);
          if (visible.empty()) {
            rows.push_back(text(i18n::tr("common.no_results")) | color(theme::Muted()));
          } else {
            for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
              const int node_index = visible[static_cast<std::size_t>(i)];
              const bool selected =
                  i == hierarchy->selected && focus != nullptr &&
                  focus->region == FocusRegion::Terminal;
              RowLayout row_layout;
              rows.push_back(render_chain_row(*hierarchy, i, node_index, selected, layout_state,
                                              &row_layout));
              state->row_layouts.push_back(std::move(row_layout));
            }
          }
        }

        auto results = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                       reflect(state->results_box) | bgcolor(theme::PanelBg());
        if (!active) {
          return PanelBody(std::move(results));
        }
        return PanelBody(vbox({std::move(header), std::move(results)}));
      }),
      handler));
}

}  // namespace tgdb
