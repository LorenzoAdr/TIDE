#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/workspace_model.hpp"
#include "symbols/call_hierarchy.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"

namespace tuide {

struct MainLayoutState;
struct RightSidebarState;

struct CallHierarchyTreeNode {
  CallHierarchyItem item;
  int depth = 0;
  int parent = -1;
  bool children_loaded = false;
  bool has_children = false;
  bool navigate_to_call_site = false;
  int nav_line = 0;
  int nav_character = 0;
  std::string nav_path;
  std::vector<int> children;
};

struct CallHierarchyViewState {
  bool active = false;
  int selected_tab = 0;
  int selected = 0;
  std::string root_label;
  std::string status;
  std::vector<CallHierarchyTreeNode> nodes;

  void clear();
};

std::vector<int> call_hierarchy_visible_rows(const CallHierarchyViewState& view);
void call_hierarchy_set_tab(CallHierarchyViewState* view, int tab,
                            const std::shared_ptr<ISymbolProvider>& symbols);

bool open_call_hierarchy_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              MainLayoutState* layout_state, RightSidebarState* sidebar,
                              const std::shared_ptr<ISymbolProvider>& symbols, int line,
                              int col, const std::string& symbol_at_cursor = {});

void navigate_to_call_hierarchy_node(WorkspaceModel* workspace, FocusManagerState* focus,
                                     MainLayoutState* layout_state,
                                     const CallHierarchyTreeNode& node);

std::string call_hierarchy_node_location(const CallHierarchyTreeNode& node);
std::string call_hierarchy_node_chain(const CallHierarchyViewState& view, int node_index);
std::vector<int> call_hierarchy_chain_indices(const CallHierarchyViewState& view, int node_index);

}  // namespace tuide
