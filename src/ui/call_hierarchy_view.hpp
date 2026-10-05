#pragma once

#include <cstdint>
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
struct WorkspaceIndexer;

enum class CallHierarchyContentKind {
  CallHierarchy,
  References,
  CausalFlow,
};

// Side of a causal-flow node. Callers and writes are upstream; reads and
// callees are downstream. Decls stay with upstream in the panel.
enum class CausalFlowSide : uint8_t {
  None = 0,
  Upstream,
  Downstream,
  Decl,
};

// u / d fold one half of the causal panel. All is the full view.
enum class CausalFlowFold : uint8_t {
  All = 0,
  Upstream,
  Downstream,
};

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
  CausalFlowSide causal_side = CausalFlowSide::None;
  // Source line preview for References rows (trimmed). Unused for call hierarchy.
  std::string preview;
  std::vector<int> children;
};

struct CallHierarchyViewState {
  bool active = false;
  CallHierarchyContentKind kind = CallHierarchyContentKind::CallHierarchy;
  int selected_tab = 0;
  int selected = 0;
  std::string root_label;
  std::string status;
  std::vector<CallHierarchyTreeNode> nodes;
  CausalFlowFold causal_fold = CausalFlowFold::All;
  // Selected causal chain shows only its if/switch conditions.
  bool causal_conditions = false;
  // When set, visible chains are those that pass through this symbol.
  std::string causal_link;
  // Click site. When causal_here is set, only chains through that line stay visible.
  bool causal_here = false;
  bool causal_anchor_known = false;
  std::string causal_anchor_path;
  int causal_anchor_line = -1;
  // Point tree is the click. All tree is the symbol search, filled on `a`.
  bool causal_showing_all = false;
  bool causal_want_all = false;
  std::vector<CallHierarchyTreeNode> causal_point_nodes;
  std::vector<CallHierarchyTreeNode> causal_all_nodes;

  void clear();
};

std::vector<int> call_hierarchy_visible_rows(const CallHierarchyViewState& view);
void call_hierarchy_set_tab(CallHierarchyViewState* view, int tab,
                            const std::shared_ptr<ISymbolProvider>& symbols);

bool open_call_hierarchy_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              MainLayoutState* layout_state, RightSidebarState* sidebar,
                              const std::shared_ptr<ISymbolProvider>& symbols, int line,
                              int col, const std::string& symbol_at_cursor = {});

bool open_references_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                          MainLayoutState* layout_state, RightSidebarState* sidebar,
                          const std::shared_ptr<ISymbolProvider>& symbols, int line, int col,
                          const std::string& symbol_at_cursor = {});

// symbol_wide false: tree-sitter at the click. true: search the symbol (`a`).
bool open_causal_flow_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                           MainLayoutState* layout_state, RightSidebarState* sidebar,
                           const std::string& symbol, WorkspaceIndexer* indexer = nullptr,
                           int editor_line = -1, const std::string& anchor_path = {},
                           bool symbol_wide = false);

// Keep the current causal tree and show only the chain that reaches target.
// With no chain, the tree stays and the status says so.
void connect_causal_flow_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              const std::string& target);

// `a` swaps the click tree and the symbol-wide tree. The symbol search starts
// the first time all is requested.
void toggle_causal_point_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              MainLayoutState* layout_state, RightSidebarState* sidebar,
                              WorkspaceIndexer* indexer);

void navigate_to_call_hierarchy_node(WorkspaceModel* workspace, FocusManagerState* focus,
                                     MainLayoutState* layout_state,
                                     const CallHierarchyTreeNode& node);

std::string call_hierarchy_node_location(const CallHierarchyTreeNode& node);
std::string call_hierarchy_node_chain(const CallHierarchyViewState& view, int node_index);
std::vector<int> call_hierarchy_chain_indices(const CallHierarchyViewState& view, int node_index);

}  // namespace tuide
