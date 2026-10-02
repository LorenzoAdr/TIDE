#include "ui/call_hierarchy_view.hpp"
#include "ui/busy_strip.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <exception>
#include <filesystem>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ai/l2_explore_a.hpp"
#include "indexer/workspace_indexer.hpp"
#include "search/workspace_search_rg.hpp"
#include "util/bundled_tools.hpp"

#include "editor/editor_context.hpp"
#include "editor/text_ops.hpp"
#include "git/git_diff.hpp"
#include "indexer/index_rules.hpp"
#include "ui/main_layout.hpp"
#include "util/monitor_log.hpp"
#include "util/path_normalize.hpp"
#include "i18n/tr.hpp"

namespace tuide {

namespace fs = std::filesystem;

namespace {

// See editor_panel.cpp's buffer_text(): unified onto the cached,
// backend-agnostic-O(n) editor_buffer_joined_source().
std::string buffer_document_text(const EditorBuffer& buffer) {
  return editor_buffer_joined_source(buffer);
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string trim_reference_preview(const std::string& line) {
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = line.find_last_not_of(" \t");
  return line.substr(start, end - start + 1);
}

std::string reference_line_preview(WorkspaceModel* workspace, const std::string& path,
                                   int line_0based,
                                   std::unordered_map<std::string, std::vector<std::string>>* cache) {
  if (path.empty() || line_0based < 0) {
    return {};
  }
  if (workspace != nullptr &&
      normalize_path(workspace->buffer.path) == normalize_path(path) &&
      line_0based < workspace->buffer.lines.size()) {
    return trim_reference_preview(workspace->buffer.lines[line_0based]);
  }
  if (cache == nullptr) {
    return {};
  }
  auto it = cache->find(path);
  if (it == cache->end()) {
    it = cache->emplace(path, load_lines_from_file(path)).first;
  }
  if (line_0based >= static_cast<int>(it->second.size())) {
    return {};
  }
  return trim_reference_preview(it->second[static_cast<std::size_t>(line_0based)]);
}

std::string symbol_base_name(const SymbolInfo& sym) {
  const std::size_t space = sym.name.find(' ');
  if (space != std::string::npos) {
    return sym.name.substr(space + 1);
  }
  return sym.name;
}

int column_of_word_in_line(const std::string& line, const std::string& word) {
  if (word.empty()) {
    return -1;
  }
  std::size_t pos = 0;
  while (pos <= line.size()) {
    const std::size_t found = line.find(word, pos);
    if (found == std::string::npos) {
      break;
    }
    const bool left_ok = found == 0 || !is_ident_char(line[found - 1]);
    const bool right_ok = found + word.size() >= line.size() ||
                          !is_ident_char(line[found + word.size()]);
    if (left_ok && right_ok) {
      return static_cast<int>(found);
    }
    pos = found + 1;
  }
  return -1;
}

int callable_name_column_on_line(const std::string& line, const std::string& name) {
  const int col = column_of_word_in_line(line, name);
  if (col >= 0) {
    return col;
  }
  const std::size_t scope = name.rfind("::");
  if (scope != std::string::npos) {
    return column_of_word_in_line(line, name.substr(scope + 2));
  }
  return -1;
}

bool function_header_on_line(const std::string& line, int* out_col) {
  if (out_col == nullptr) {
    return false;
  }
  const std::size_t paren = line.find('(');
  if (paren == std::string::npos) {
    return false;
  }
  int end = static_cast<int>(paren) - 1;
  while (end >= 0 && std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(end)]))) {
    --end;
  }
  if (end < 0) {
    return false;
  }
  int start = end;
  while (start >= 0) {
    const char c = line[static_cast<std::size_t>(start)];
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':' || c == '~') {
      --start;
      continue;
    }
    break;
  }
  ++start;
  if (start > end) {
    return false;
  }
  const std::string candidate = line.substr(static_cast<std::size_t>(start),
                                            static_cast<std::size_t>(end - start + 1));
  static const char* kKeywords[] = {"if",      "for",     "while",   "switch", "catch",
                                    "return",  "sizeof",  "static_cast", "dynamic_cast",
                                    "reinterpret_cast", "const_cast"};
  for (const char* kw : kKeywords) {
    if (candidate == kw) {
      return false;
    }
  }
  const int col = callable_name_column_on_line(line, candidate);
  *out_col = col >= 0 ? col : start;
  return true;
}

bool find_enclosing_callable_in_buffer(const EditorBuffer& buffer, int line_0, int* out_line,
                                       int* out_col) {
  if (out_line == nullptr || out_col == nullptr || buffer.lines.empty()) {
    return false;
  }
  const int start = std::max(0, std::min(line_0, static_cast<int>(buffer.lines.size()) - 1));
  for (int line = start; line >= 0; --line) {
    int col = 0;
    if (!function_header_on_line(buffer.lines[static_cast<std::size_t>(line)], &col)) {
      continue;
    }
    *out_line = line;
    *out_col = col;
    return true;
  }
  return false;
}

void set_callable_position(const EditorBuffer& buffer, int line_0, const std::string& name,
                           int* out_line, int* out_col) {
  if (out_line == nullptr || out_col == nullptr) {
    return;
  }
  *out_line = line_0;
  *out_col = 0;
  if (line_0 < 0 || line_0 >= static_cast<int>(buffer.lines.size())) {
    return;
  }
  const int col = callable_name_column_on_line(buffer.lines[static_cast<std::size_t>(line_0)], name);
  if (col >= 0) {
    *out_col = col;
  }
}

bool resolve_enclosing_callable_position(const std::shared_ptr<ISymbolProvider>& symbols,
                                         const std::string& path, int line_0, int col_0,
                                         const EditorBuffer& buffer, int* out_line,
                                         int* out_col) {
  if (out_line == nullptr || out_col == nullptr) {
    return false;
  }

  if (symbols != nullptr && !path.empty()) {
    const std::vector<SymbolInfo> file_symbols = symbols->symbols_for_file(path);
    const auto chain = scope_chain_at_line(file_symbols, line_0);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      const SymbolInfo* sym = *it;
      if (sym == nullptr) {
        continue;
      }
      if (sym->kind != SymbolKind::kFunction && sym->kind != SymbolKind::kMethod) {
        continue;
      }
      const int sym_line_0 = sym->line - 1;
      if (sym_line_0 > line_0) {
        continue;
      }
      if (line_0 == sym_line_0) {
        *out_line = sym_line_0;
        *out_col = col_0;
        return true;
      }
      set_callable_position(buffer, sym_line_0, symbol_base_name(*sym), out_line, out_col);
      return true;
    }
  }

  return find_enclosing_callable_in_buffer(buffer, line_0, out_line, out_col);
}

bool resolve_call_hierarchy_position(const std::shared_ptr<ISymbolProvider>& symbols,
                                     const std::string& path, int line_0, int col_0,
                                     const std::string& symbol_at_cursor,
                                     const EditorBuffer& buffer, int* out_line, int* out_col) {
  if (out_line == nullptr || out_col == nullptr) {
    return false;
  }
  *out_line = line_0;
  *out_col = col_0;

  if (symbols == nullptr || path.empty()) {
    return true;
  }

  // Clic sobre un identificador: el language server resuelve el símbolo en esa posición
  // (declaración o sitio de uso).
  if (!symbol_at_cursor.empty()) {
    return true;
  }

  // Clic en zona vacía: usar la función/método contenedor.
  if (resolve_enclosing_callable_position(symbols, path, line_0, col_0, buffer, out_line,
                                          out_col)) {
    return true;
  }
  return true;
}

bool try_prepare_enclosing_call_hierarchy(const std::shared_ptr<ISymbolProvider>& symbols,
                                          CallHierarchyParams* params, int cursor_line,
                                          int cursor_col, const EditorBuffer& buffer,
                                          std::vector<CallHierarchyItem>* out_roots,
                                          int* resolved_line, int* resolved_col) {
  if (symbols == nullptr || params == nullptr || out_roots == nullptr ||
      resolved_line == nullptr || resolved_col == nullptr) {
    return false;
  }

  int enclosing_line = cursor_line;
  int enclosing_col = cursor_col;
  if (!resolve_enclosing_callable_position(symbols, params->path, cursor_line, cursor_col, buffer,
                                           &enclosing_line, &enclosing_col)) {
    return false;
  }
  if (enclosing_line == params->line && enclosing_col == params->character) {
    return false;
  }

  params->line = enclosing_line;
  params->character = enclosing_col;
  TUIDE_MON_SCOPE("editor", "call_hierarchy.prepare_enclosing");
  *out_roots = symbols->prepare_call_hierarchy(*params);
  if (out_roots->empty()) {
    return false;
  }
  *resolved_line = enclosing_line;
  *resolved_col = enclosing_col;
  return true;
}

void append_leaf_rows(const CallHierarchyViewState& view, int node_index,
                      std::vector<int>* rows, std::vector<uint8_t>* visited) {
  if (view.nodes.empty() || node_index < 0 ||
      node_index >= static_cast<int>(view.nodes.size()) || rows == nullptr ||
      visited == nullptr) {
    return;
  }
  if ((*visited)[static_cast<std::size_t>(node_index)] != 0) {
    return;
  }
  (*visited)[static_cast<std::size_t>(node_index)] = 1;

  const CallHierarchyTreeNode& node = view.nodes[static_cast<std::size_t>(node_index)];
  if (node.children.empty()) {
    if (node_index > 0) {
      rows->push_back(node_index);
    }
    return;
  }
  for (int child : node.children) {
    append_leaf_rows(view, child, rows, visited);
  }
}

void append_visible_rows(const CallHierarchyViewState& view, int node_index,
                         std::vector<int>* rows, std::vector<uint8_t>* visited) {
  if (view.nodes.empty() || node_index < 0 ||
      node_index >= static_cast<int>(view.nodes.size()) || rows == nullptr ||
      visited == nullptr) {
    return;
  }
  if ((*visited)[static_cast<std::size_t>(node_index)] != 0) {
    return;
  }
  (*visited)[static_cast<std::size_t>(node_index)] = 1;
  rows->push_back(node_index);
  for (int child : view.nodes[static_cast<std::size_t>(node_index)].children) {
    append_visible_rows(view, child, rows, visited);
  }
}

void append_visible_rows(const CallHierarchyViewState& view, int node_index,
                         std::vector<int>* rows) {
  if (view.nodes.empty() || node_index < 0 ||
      node_index >= static_cast<int>(view.nodes.size()) || rows == nullptr) {
    return;
  }
  std::vector<uint8_t> visited(view.nodes.size(), 0);
  append_visible_rows(view, node_index, rows, &visited);
}

std::string hierarchy_node_key(const CallHierarchyItem& item) {
  return item.path + '\n' + std::to_string(item.line) + '\n' + std::to_string(item.character);
}

void assign_node_navigation(CallHierarchyTreeNode* node, const CallHierarchyItem& item,
                            const std::string& parent_path, int tab) {
  if (node == nullptr) {
    return;
  }
  node->nav_path.clear();
  if (item.has_call_site) {
    node->navigate_to_call_site = true;
    node->nav_line = item.call_site_line;
    node->nav_character = item.call_site_character;
    if (tab == 1 && !parent_path.empty()) {
      node->nav_path = parent_path;
    }
    return;
  }
  node->navigate_to_call_site = false;
  node->nav_line = item.line;
  node->nav_character = item.character;
}

void update_hierarchy_status(CallHierarchyViewState* view) {
  if (view == nullptr || view->nodes.empty()) {
    return;
  }
  std::vector<int> leaves;
  std::vector<uint8_t> visited(view->nodes.size(), 0);
  append_leaf_rows(*view, 0, &leaves, &visited);
  view->status = i18n::tr_fmt("status.call_hierarchy.incoming_count", {std::to_string(leaves.size())});
}

void load_node_children(CallHierarchyViewState* view, int node_index,
                        const std::shared_ptr<ISymbolProvider>& symbols) {
  if (view == nullptr || symbols == nullptr || node_index < 0 ||
      node_index >= static_cast<int>(view->nodes.size())) {
    return;
  }

  CallHierarchyTreeNode& node = view->nodes[static_cast<std::size_t>(node_index)];
  if (node.children_loaded) {
    return;
  }

  TUIDE_MON_SCOPE("editor", "call_hierarchy.incoming_calls");
  const std::vector<CallHierarchyItem> items = symbols->incoming_calls(node.item);
  std::vector<int> new_children;
  new_children.reserve(items.size());
  const int parent_depth = node.depth;
  const std::string parent_path = node.item.path;
  for (const CallHierarchyItem& item : items) {
    if (!item.valid) {
      continue;
    }
    CallHierarchyTreeNode child;
    child.item = item;
    child.depth = parent_depth + 1;
    child.parent = node_index;
    child.has_children = true;
    assign_node_navigation(&child, item, parent_path, 0);
    view->nodes.push_back(std::move(child));
    new_children.push_back(static_cast<int>(view->nodes.size()) - 1);
  }
  // Re-fetch after push_back: references into nodes[] are invalidated on reallocation.
  CallHierarchyTreeNode& updated = view->nodes[static_cast<std::size_t>(node_index)];
  updated.children = std::move(new_children);
  updated.children_loaded = true;
  updated.has_children = !updated.children.empty();
}

void expand_hierarchy_node(CallHierarchyViewState* view, int node_index,
                           const std::shared_ptr<ISymbolProvider>& symbols,
                           std::vector<std::string>* ancestry, int depth) {
  constexpr int kMaxDepth = 32;
  if (view == nullptr || symbols == nullptr || ancestry == nullptr || depth > kMaxDepth ||
      node_index < 0 || node_index >= static_cast<int>(view->nodes.size())) {
    return;
  }

  CallHierarchyTreeNode& node = view->nodes[static_cast<std::size_t>(node_index)];
  const std::string key = hierarchy_node_key(node.item);
  if (std::find(ancestry->begin(), ancestry->end(), key) != ancestry->end()) {
    return;
  }
  ancestry->push_back(key);

  load_node_children(view, node_index, symbols);
  const std::vector<int> children =
      view->nodes[static_cast<std::size_t>(node_index)].children;
  for (int child : children) {
    expand_hierarchy_node(view, child, symbols, ancestry, depth + 1);
  }
  ancestry->pop_back();
}

void expand_hierarchy_tree(CallHierarchyViewState* view,
                           const std::shared_ptr<ISymbolProvider>& symbols) {
  if (view == nullptr || view->nodes.empty() || symbols == nullptr) {
    return;
  }
  std::vector<std::string> ancestry;
  expand_hierarchy_node(view, 0, symbols, &ancestry, 0);
  update_hierarchy_status(view);
}

}  // namespace

void CallHierarchyViewState::clear() {
  active = false;
  kind = CallHierarchyContentKind::CallHierarchy;
  selected_tab = 0;
  selected = 0;
  root_label.clear();
  status.clear();
  nodes.clear();
  causal_fold = CausalFlowFold::All;
  causal_conditions = false;
  causal_link.clear();
}

bool causal_node_matches(const std::string& name, const std::string& symbol) {
  if (symbol.empty() || name.empty()) {
    return false;
  }
  if (name == symbol) {
    return true;
  }
  const std::string suffix = "::" + symbol;
  return name.size() > suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool causal_chain_mentions(const CallHierarchyViewState& view, int node_index,
                           const std::string& symbol) {
  for (int index : call_hierarchy_chain_indices(view, node_index)) {
    if (index < 0 || index >= static_cast<int>(view.nodes.size())) {
      continue;
    }
    if (causal_node_matches(view.nodes[static_cast<std::size_t>(index)].item.name, symbol)) {
      return true;
    }
  }
  return false;
}

std::vector<int> call_hierarchy_visible_rows(const CallHierarchyViewState& view) {
  std::vector<int> rows;
  if (!view.active || view.nodes.empty()) {
    return rows;
  }
  if (view.kind == CallHierarchyContentKind::References) {
    rows.reserve(view.nodes.size());
    for (int i = 0; i < static_cast<int>(view.nodes.size()); ++i) {
      rows.push_back(i);
    }
    return rows;
  }
  std::vector<uint8_t> visited(view.nodes.size(), 0);
  append_leaf_rows(view, 0, &rows, &visited);
  if (view.kind != CallHierarchyContentKind::CausalFlow) {
    return rows;
  }
  std::vector<int> upstream;
  std::vector<int> downstream;
  upstream.reserve(rows.size());
  for (int node_index : rows) {
    if (!view.causal_link.empty() &&
        !causal_chain_mentions(view, node_index, view.causal_link)) {
      continue;
    }
    const CausalFlowSide side = view.nodes[static_cast<std::size_t>(node_index)].causal_side;
    if (view.causal_fold == CausalFlowFold::Upstream && side == CausalFlowSide::Downstream) {
      continue;
    }
    if (view.causal_fold == CausalFlowFold::Downstream && side != CausalFlowSide::Downstream) {
      continue;
    }
    if (side == CausalFlowSide::Downstream) {
      downstream.push_back(node_index);
    } else {
      upstream.push_back(node_index);
    }
  }
  if (view.causal_fold != CausalFlowFold::Downstream) {
    upstream.insert(upstream.end(), downstream.begin(), downstream.end());
    return upstream;
  }
  return downstream;
}

void connect_causal_flow_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              const std::string& target) {
  if (view == nullptr || !view->active || view->kind != CallHierarchyContentKind::CausalFlow ||
      target.empty()) {
    return;
  }
  view->selected = 0;
  view->causal_fold = CausalFlowFold::All;
  if (target == view->root_label) {
    view->causal_link.clear();
    return;
  }
  std::vector<int> leaves;
  std::vector<uint8_t> visited(view->nodes.size(), 0);
  append_leaf_rows(*view, 0, &leaves, &visited);
  int matches = 0;
  for (int node_index : leaves) {
    if (causal_chain_mentions(*view, node_index, target)) {
      ++matches;
    }
  }
  if (matches == 0) {
    view->causal_link.clear();
    view->status = i18n::tr_fmt("status.causal_flow.no_path", {view->root_label, target});
    if (workspace != nullptr) {
      workspace->status_message = view->status;
    }
    return;
  }
  view->causal_link = target;
  view->status = i18n::tr_fmt("status.causal_flow.path", {view->root_label, target});
  if (workspace != nullptr) {
    workspace->status_message = view->status;
  }
}

void call_hierarchy_set_tab(CallHierarchyViewState* view, int tab,
                            const std::shared_ptr<ISymbolProvider>& symbols) {
  if (view == nullptr || !view->active || view->nodes.empty() || symbols == nullptr ||
      view->kind != CallHierarchyContentKind::CallHierarchy) {
    return;
  }
  view->selected_tab = std::max(0, std::min(tab, 1));
  CallHierarchyTreeNode root = std::move(view->nodes.front());
  root.children_loaded = false;
  root.children.clear();
  root.has_children = true;
  view->nodes.assign(1, std::move(root));
  view->selected = 0;
  expand_hierarchy_tree(view, symbols);
}

std::string call_hierarchy_node_location(const CallHierarchyTreeNode& node) {
  const int line = node.navigate_to_call_site ? node.nav_line : node.item.line;
  const int character = node.navigate_to_call_site ? node.nav_character : node.item.character;
  const std::string& path = node.nav_path.empty() ? node.item.path : node.nav_path;
  std::string label = fs::path(path).filename().string();
  label += ":" + std::to_string(line + 1) + ":" + std::to_string(character + 1);
  return label;
}

std::string call_hierarchy_node_chain(const CallHierarchyViewState& view, int node_index) {
  if (node_index < 0 || node_index >= static_cast<int>(view.nodes.size())) {
    return {};
  }
  std::vector<std::string> names;
  int current = node_index;
  while (current >= 0) {
    const auto& node = view.nodes[static_cast<std::size_t>(current)];
    std::string label = node.item.name;
    if (!node.item.detail.empty()) {
      label += " (" + node.item.detail + ")";
    }
    names.push_back(std::move(label));
    current = node.parent;
  }
  std::reverse(names.begin(), names.end());
  std::string chain;
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (i > 0) {
      chain += " -> ";
    }
    chain += names[i];
  }
  return chain;
}

std::vector<int> call_hierarchy_chain_indices(const CallHierarchyViewState& view, int node_index) {
  std::vector<int> indices;
  if (node_index < 0 || node_index >= static_cast<int>(view.nodes.size())) {
    return indices;
  }
  std::vector<uint8_t> visited(view.nodes.size(), 0);
  int current = node_index;
  while (current >= 0 && current < static_cast<int>(view.nodes.size()) &&
         visited[static_cast<std::size_t>(current)] == 0) {
    visited[static_cast<std::size_t>(current)] = 1;
    indices.push_back(current);
    current = view.nodes[static_cast<std::size_t>(current)].parent;
  }
  std::reverse(indices.begin(), indices.end());
  return indices;
}

bool open_call_hierarchy_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                              MainLayoutState* layout_state, RightSidebarState* sidebar,
                              const std::shared_ptr<ISymbolProvider>& symbols, int line,
                              int col, const std::string& symbol_at_cursor) {
  if (view == nullptr || workspace == nullptr || sidebar == nullptr) {
    return false;
  }
  view->clear();

  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    workspace->status_message = i18n::tr("status.lsp_disabled");
    return false;
  }

  workspace->ensure_buffer();
  CallHierarchyParams params;
  params.path = workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  params.text = buffer_document_text(workspace->buffer);
  if (params.path.empty()) {
    workspace->status_message = i18n::tr("status.no_active_file");
    return false;
  }
  if (symbols == nullptr || !symbols->supports_call_hierarchy(params.path)) {
    workspace->status_message = i18n::tr("status.call_hierarchy.unavailable");
    return false;
  }

  int resolved_line = line;
  int resolved_col = col;
  resolve_call_hierarchy_position(symbols, params.path, line, col, symbol_at_cursor,
                                  workspace->buffer, &resolved_line, &resolved_col);
  params.line = resolved_line;
  params.character = resolved_col;

  set_busy_spinner(layout_state, BusyActivity::CallHierarchy);
  TUIDE_MON_SCOPE("editor", "call_hierarchy.prepare");
  std::vector<CallHierarchyItem> roots = symbols->prepare_call_hierarchy(params);
  if (roots.empty()) {
    try_prepare_enclosing_call_hierarchy(symbols, &params, line, col, workspace->buffer, &roots,
                                         &resolved_line, &resolved_col);
  }
  if (roots.empty()) {
    clear_busy(layout_state);
    workspace->status_message = i18n::tr("status.no_call_hierarchy_scope");
    return false;
  }

  view->active = true;
  view->kind = CallHierarchyContentKind::CallHierarchy;
  view->selected_tab = 0;
  view->selected = 0;

  CallHierarchyTreeNode root;
  root.item = roots.front();
  root.depth = 0;
  root.parent = -1;
  root.has_children = true;
  root.navigate_to_call_site = true;
  root.nav_line = resolved_line;
  root.nav_character = resolved_col;
  view->nodes.push_back(std::move(root));
  expand_hierarchy_tree(view, symbols);
  clear_busy(layout_state);

  std::string label = view->nodes.front().item.name;
  if (!view->nodes.front().item.detail.empty()) {
    label += " — " + view->nodes.front().item.detail;
  }
  view->root_label = label;

  layout_state->console_visible = true;
  layout_state->console_tabs.selected_tab = ConsolePanelTabs::kCallHierarchy;
  layout_state->right_panel_active_section = 0;
  layout_state->text_input_focus = TextInputFocus::None;
  if (layout_state != nullptr) {
    layout_state->focus_sync_needed = true;
    wake_console_panel(layout_state);
  }
  workspace->status_message = i18n::tr_fmt("status.call_hierarchy.active", {label});
  return true;
}

bool open_references_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                          MainLayoutState* layout_state, RightSidebarState* sidebar,
                          const std::shared_ptr<ISymbolProvider>& symbols, int line, int col,
                          const std::string& symbol_at_cursor) {
  if (view == nullptr || workspace == nullptr || sidebar == nullptr) {
    return false;
  }
  view->clear();

  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    workspace->status_message = i18n::tr("status.lsp_disabled");
    return false;
  }

  workspace->ensure_buffer();
  NavigationParams params;
  params.path = workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  params.text = buffer_document_text(workspace->buffer);
  if (params.path.empty()) {
    workspace->status_message = i18n::tr("status.no_active_file");
    return false;
  }
  if (symbols == nullptr || !symbols->supports_references(params.path)) {
    workspace->status_message = i18n::tr("status.references.unavailable");
    return false;
  }

  params.line = line;
  params.character = col;

  set_busy_spinner(layout_state, BusyActivity::FindReferences);
  TUIDE_MON_SCOPE("editor", "references.find");
  const std::vector<SourceLocation> locations = symbols->find_references(params, true);
  clear_busy(layout_state);
  if (locations.empty()) {
    workspace->status_message = i18n::tr("status.references.none");
    return false;
  }

  view->active = true;
  view->kind = CallHierarchyContentKind::References;
  view->selected_tab = 0;
  view->selected = 0;

  const std::string label =
      !symbol_at_cursor.empty() ? symbol_at_cursor : i18n::tr("panel.references.untitled");
  view->root_label = label;
  view->status =
      i18n::tr_fmt("status.references.count", {std::to_string(locations.size())});

  view->nodes.reserve(locations.size());
  std::unordered_map<std::string, std::vector<std::string>> preview_cache;
  for (const SourceLocation& loc : locations) {
    if (!loc.valid || loc.path.empty()) {
      continue;
    }
    CallHierarchyTreeNode node;
    node.item.valid = true;
    node.item.name = fs::path(loc.path).filename().string();
    node.item.path = loc.path;
    node.item.line = loc.line;
    node.item.character = loc.character;
    node.item.kind = SymbolKind::kVariable;
    node.depth = 0;
    node.parent = -1;
    node.children_loaded = true;
    node.has_children = false;
    node.navigate_to_call_site = true;
    node.nav_line = loc.line;
    node.nav_character = loc.character;
    node.nav_path = loc.path;
    node.preview = reference_line_preview(workspace, loc.path, loc.line, &preview_cache);
    view->nodes.push_back(std::move(node));
  }

  if (view->nodes.empty()) {
    view->clear();
    workspace->status_message = i18n::tr("status.references.none");
    return false;
  }

  if (layout_state != nullptr) {
    layout_state->console_visible = true;
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kCallHierarchy;
    layout_state->right_panel_active_section = 0;
    layout_state->text_input_focus = TextInputFocus::None;
    layout_state->focus_sync_needed = true;
    wake_console_panel(layout_state);
  }
  workspace->status_message = i18n::tr_fmt("status.references.active", {label});
  return true;
}

void navigate_to_call_hierarchy_node(WorkspaceModel* workspace, FocusManagerState* focus,
                                     MainLayoutState* layout_state,
                                     const CallHierarchyTreeNode& node) {
  if (workspace == nullptr || !node.item.valid) {
    return;
  }
  const int line = node.navigate_to_call_site ? node.nav_line : node.item.line;
  const int character = node.navigate_to_call_site ? node.nav_character : node.item.character;
  const std::string& path = node.nav_path.empty() ? node.item.path : node.nav_path;
  if (path.empty()) {
    return;
  }
  int visible_lines = 24;
  if (layout_state != nullptr && layout_state->primary_editor.visible_line_count) {
    visible_lines = std::max(1, layout_state->primary_editor.visible_line_count());
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(path, line, character);
  ensure_scroll_centered(&workspace->buffer, visible_lines);
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
}

std::string absolute_causal_path(const std::string& workspace_root, const std::string& path) {
  if (path.empty()) {
    return {};
  }
  fs::path file(path);
  if (file.is_absolute() || workspace_root.empty()) {
    return file.lexically_normal().string();
  }
  return (fs::path(workspace_root) / file).lexically_normal().string();
}

std::string causal_path_hint(const std::string& workspace_root, const std::string& absolute_path) {
  if (absolute_path.empty() || workspace_root.empty()) {
    return absolute_path;
  }
  std::error_code ec;
  const fs::path rel = fs::relative(fs::path(absolute_path), fs::path(workspace_root), ec);
  if (ec) {
    return absolute_path;
  }
  return rel.generic_string();
}

CausalFlowSide causal_flow_side(ACausalNodeKind kind) {
  switch (kind) {
    case ACausalNodeKind::Write:
    case ACausalNodeKind::Caller:
    case ACausalNodeKind::Guard:
      return CausalFlowSide::Upstream;
    case ACausalNodeKind::Read:
    case ACausalNodeKind::Callee:
      return CausalFlowSide::Downstream;
    case ACausalNodeKind::Decl:
      return CausalFlowSide::Decl;
    case ACausalNodeKind::Root:
      return CausalFlowSide::None;
  }
  return CausalFlowSide::None;
}

SymbolKind causal_symbol_kind(ACausalNodeKind kind) {
  switch (kind) {
    case ACausalNodeKind::Write:
    case ACausalNodeKind::Caller:
    case ACausalNodeKind::Callee:
    case ACausalNodeKind::Guard:
      return SymbolKind::kFunction;
    case ACausalNodeKind::Root:
    case ACausalNodeKind::Decl:
    case ACausalNodeKind::Read:
      return SymbolKind::kVariable;
  }
  return SymbolKind::kVariable;
}

int append_causal_node(CallHierarchyViewState* view, const ACausalFlowNode& node,
                       const std::string& workspace_root, int parent, int depth) {
  CallHierarchyTreeNode tree;
  tree.item.valid = true;
  tree.item.name = node.name.empty() ? "?" : node.name;
  tree.item.detail = node.detail;
  tree.item.path = absolute_causal_path(workspace_root, node.path);
  tree.item.line = node.line > 0 ? node.line - 1 : 0;
  tree.item.character = 0;
  tree.item.kind = causal_symbol_kind(node.kind);
  tree.depth = depth;
  tree.parent = parent;
  tree.children_loaded = true;
  tree.navigate_to_call_site = !tree.item.path.empty();
  tree.nav_line = tree.item.line;
  tree.nav_character = 0;
  tree.nav_path = tree.item.path;
  tree.preview = node.preview;
  tree.causal_side = causal_flow_side(node.kind);
  view->nodes.push_back(std::move(tree));
  const int index = static_cast<int>(view->nodes.size()) - 1;
  std::vector<int> children;
  children.reserve(node.children.size());
  for (const ACausalFlowNode& child : node.children) {
    children.push_back(append_causal_node(view, child, workspace_root, index, depth + 1));
  }
  CallHierarchyTreeNode& stored = view->nodes[static_cast<std::size_t>(index)];
  stored.children = std::move(children);
  stored.has_children = !stored.children.empty();
  return index;
}

bool open_causal_flow_view(CallHierarchyViewState* view, WorkspaceModel* workspace,
                           MainLayoutState* layout_state, RightSidebarState* sidebar,
                           const std::string& symbol, WorkspaceIndexer* indexer, int editor_line,
                           const std::string& anchor_path) {
  if (view == nullptr || workspace == nullptr || sidebar == nullptr) {
    return false;
  }
  view->clear();
  if (symbol.empty()) {
    return false;
  }
  if (workspace->root.empty()) {
    workspace->status_message = i18n::tr("status.causal_flow.no_workspace");
    return false;
  }
  const auto rg = resolve_rg();
  const bool have_rg = rg.has_value();

  workspace->ensure_buffer();
  const std::string active =
      workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  const std::string path_hint =
      causal_path_hint(workspace->root, anchor_path.empty() ? active : anchor_path);
  const int anchor_line = editor_line >= 0 ? editor_line + 1 : 0;

  const std::string root = workspace->root;
  std::shared_ptr<const std::vector<std::string>> indexed_files;
  if (!have_rg && indexer != nullptr) {
    if (const auto snap = indexer->snapshot()) {
      std::error_code ec;
      const std::string snap_root = fs::absolute(snap->workspace_root, ec).lexically_normal().string();
      const std::string want_root = fs::absolute(root, ec).lexically_normal().string();
      if (snap_root == want_root && !snap->files.empty()) {
        indexed_files = std::shared_ptr<const std::vector<std::string>>(snap, &snap->files);
      }
    }
  }
  if (!have_rg && indexed_files == nullptr) {
    workspace->status_message = i18n::tr("status.causal_flow.unavailable");
    return false;
  }

  set_busy_spinner(layout_state, BusyActivity::CallHierarchy);
  view->active = true;
  view->kind = CallHierarchyContentKind::CausalFlow;
  view->root_label = symbol;
  view->status = symbol;
  if (layout_state != nullptr) {
    layout_state->console_visible = true;
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kCallHierarchy;
    wake_console_panel(layout_state);
  }

  static std::atomic<uint64_t> causal_gen{0};
  const uint64_t ticket = ++causal_gen;
  const std::string rg_binary = have_rg ? rg->binary_path : std::string{};

  auto apply = [view, workspace, layout_state, root, symbol, ticket](ACausalFlowTree tree) {
    if (ticket != causal_gen.load()) {
      return;
    }
    view->nodes.clear();
    view->active = true;
    view->kind = CallHierarchyContentKind::CausalFlow;
    view->selected_tab = 0;
    view->selected = 0;
    view->root_label = symbol;
    view->causal_fold = CausalFlowFold::All;
    view->causal_conditions = false;
    view->causal_link.clear();
    append_causal_node(view, tree.root, root, -1, 0);
    int leaves = 0;
    for (std::size_t i = 1; i < view->nodes.size(); ++i) {
      if (view->nodes[i].children.empty()) {
        ++leaves;
      }
    }
    if (leaves == 0) {
      view->status = i18n::tr_fmt("status.causal_flow.none", {symbol});
      workspace->status_message = view->status;
    } else {
      view->status = i18n::tr_fmt("status.causal_flow.count", {std::to_string(leaves)});
      workspace->status_message = i18n::tr_fmt("status.causal_flow.active", {symbol});
    }
    clear_busy(layout_state);
    if (layout_state != nullptr) {
      layout_state->focus_sync_needed = true;
      wake_console_panel(layout_state);
    }
  };

  if (workspace->enqueue_ui_task == nullptr) {
    clear_busy(layout_state);
    return false;
  }

  std::thread([workspace, root, symbol, path_hint, anchor_line, have_rg, rg_binary, indexed_files,
               apply]() mutable {
    auto scoped_files = std::make_shared<std::vector<std::string>>();
    if (indexed_files != nullptr) {
      for (const std::string& path : *indexed_files) {
        if (path.rfind("src/", 0) == 0 || path.rfind("include/", 0) == 0 ||
            path.rfind("lib/", 0) == 0) {
          scoped_files->push_back(path);
        }
      }
      if (!path_hint.empty()) {
        const auto it = std::find(scoped_files->begin(), scoped_files->end(), path_hint);
        if (it != scoped_files->end() && it != scoped_files->begin()) {
          std::rotate(scoped_files->begin(), it, it + 1);
        }
      }
    }
    auto search = [root, have_rg, rg_binary, indexed_files, scoped_files](const std::string& needle)
        -> std::vector<ATrailSearchHit> {
      std::vector<ATrailSearchHit> hits;
      if (needle.empty()) {
        return hits;
      }
      WorkspaceSearchOptions opts;
      opts.workspace_root = root;
      opts.needle = needle;
      std::vector<WorkspaceSearchResult> results;
      bool used_rg = false;
      if (have_rg) {
        int files_scanned = 0;
        std::atomic<pid_t> child{0};
        used_rg = search_workspace_rg(opts, rg_binary, [] { return false; }, &child, &results,
                                      &files_scanned);
      }
      if (!used_rg) {
        std::shared_ptr<const std::vector<std::string>> files = indexed_files;
        if (scoped_files != nullptr && !scoped_files->empty()) {
          files = scoped_files;
        }
        if (files == nullptr || files->empty()) {
          return hits;
        }
        opts.files_ref = files;
        results = search_workspace(opts);
      }
      hits.reserve(results.size());
      for (const WorkspaceSearchResult& result : results) {
        ATrailSearchHit hit;
        hit.path = result.file;
        hit.line = result.line;
        hit.preview = result.preview;
        hits.push_back(std::move(hit));
      }
      return hits;
    };
    ACausalFlowTree tree;
    try {
      tree = a_causal_flow_build(root, symbol, path_hint, search, kACausalFlowMaxWrites,
                                 kACausalMaxChains, kACausalUpstreamDepth, anchor_line);
    } catch (const std::exception&) {
    }
    workspace->enqueue_ui_task([apply, tree = std::move(tree)]() mutable { apply(std::move(tree)); });
  }).detach();
  return true;
}

}  // namespace tuide
