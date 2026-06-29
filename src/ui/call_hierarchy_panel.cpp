#include "ui/call_hierarchy_panel.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/call_hierarchy_view.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/search_panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct CallHierarchyPanelState {
  Box results_box;
};

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
        focus->region = FocusRegion::RightPanel;
      }
    }

    if (sidebar->selected_tab != RightSidebarTabs::kCallHierarchy) {
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

      if (event == Event::Escape) {
        hierarchy->clear();
        return true;
      }
      if (event == Event::Character('h') || event == Event::Character('1')) {
        call_hierarchy_set_tab(hierarchy, 0, symbols);
        return true;
      }
      if (event == Event::Character('l') || event == Event::Character('2')) {
        call_hierarchy_set_tab(hierarchy, 1, symbols);
        return true;
      }
      if (event == Event::Tab) {
        call_hierarchy_set_tab(hierarchy, hierarchy->selected_tab == 0 ? 1 : 0, symbols);
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
          navigate_to_call_hierarchy_node(
              workspace, focus, layout_state,
              hierarchy->nodes[static_cast<std::size_t>(node_index)]);
        }
        return true;
      }
      if (event.is_mouse() && event.mouse().button == Mouse::Left &&
          event.mouse().motion == Mouse::Pressed) {
        const auto& m = event.mouse();
        if (state->results_box.Contain(m.x, m.y)) {
          if (focus != nullptr) {
            focus->region = FocusRegion::RightPanel;
          }
          const int visual_row = m.y - state->results_box.y_min;
          if (visual_row >= 0 && visual_row < static_cast<int>(visible.size())) {
            hierarchy->selected = visual_row;
            const int node_index = visible[static_cast<std::size_t>(visual_row)];
            navigate_to_call_hierarchy_node(
                workspace, focus, layout_state,
                hierarchy->nodes[static_cast<std::size_t>(node_index)]);
          }
          return true;
        }
      }
      return focus != nullptr && focus->region == FocusRegion::RightPanel;
    }

    if (focus != nullptr && focus->region == FocusRegion::RightPanel) {
      return event == Event::Escape;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->call_hierarchy_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(
      Renderer([state, sidebar, focus] {
        CallHierarchyViewState* hierarchy =
            sidebar != nullptr ? &sidebar->call_hierarchy : nullptr;
        const bool active = hierarchy != nullptr && hierarchy->active;

        Element header;
        Elements rows;
        if (!active) {
          header = vbox({
              text(" Jerarquía de llamadas") | color(theme::Accent()) | bold,
              separator(),
              text(" Clic derecho en el editor → Jerarquía de llamadas") | color(theme::Muted()) |
                  size(HEIGHT, EQUAL, 1),
          });
          rows.push_back(text("(sin jerarquía activa)") | color(theme::Muted()));
        } else {
          const bool incoming_tab = hierarchy->selected_tab == 0;
          Element incoming_tab_el =
              text(incoming_tab ? "> Entrantes" : "  Entrantes") |
              color(incoming_tab ? theme::Accent() : theme::Muted()) | bold;
          Element outgoing_tab_el =
              text(!incoming_tab ? "> Salientes" : "  Salientes") |
              color(!incoming_tab ? theme::Accent() : theme::Muted()) | bold;
          header = vbox({
              text(" " + hierarchy->root_label) | color(theme::Accent()) | bold,
              hbox({incoming_tab_el, text("   "), outgoing_tab_el}),
              separator(),
              text(" " + hierarchy->status + "  Enter: ir  Tab: pestaña  Esc: limpiar") |
                  color(theme::Muted()) | size(HEIGHT, EQUAL, 1),
          });

          const std::vector<int> visible = call_hierarchy_visible_rows(*hierarchy);
          if (visible.empty()) {
            rows.push_back(text("(sin resultados)") | color(theme::Muted()));
          } else {
            for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
              const int node_index = visible[static_cast<std::size_t>(i)];
              const CallHierarchyTreeNode& node =
                  hierarchy->nodes[static_cast<std::size_t>(node_index)];
              const bool selected =
                  i == hierarchy->selected && focus->region == FocusRegion::RightPanel;
              const std::string indent(static_cast<std::size_t>(node.depth) * 2, ' ');
              const std::string prefix = node_index == 0 ? "● " : "- ";
              std::string line = node.item.name;
              if (!node.item.detail.empty()) {
                line += "  " + node.item.detail;
              }
              line += "  @ " + call_hierarchy_node_location(node);
              Element row = text(" " + indent + prefix + line) | color(theme::Header());
              if (selected) {
                row = row | inverted | bold;
              }
              rows.push_back(std::move(row));
            }
          }
        }

        auto results = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                       reflect(state->results_box) | bgcolor(theme::PanelBg());
        return PanelBody(vbox({std::move(header), std::move(results)}));
      }),
      handler));
}

}  // namespace tgdb
