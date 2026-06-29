#include "ui/call_hierarchy_view.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include "editor/editor_context.hpp"
#include "editor/text_ops.hpp"
#include "indexer/index_rules.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

namespace fs = std::filesystem;

namespace {

std::string buffer_document_text(const EditorBuffer& buffer) {
  std::string text;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    if (i > 0) {
      text.push_back('\n');
    }
    text += buffer.lines[i];
  }
  return text;
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
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

  // Clic sobre un identificador: clangd resuelve el símbolo en esa posición
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
  *out_roots = symbols->prepare_call_hierarchy(*params);
  if (out_roots->empty()) {
    return false;
  }
  *resolved_line = enclosing_line;
  *resolved_col = enclosing_col;
  return true;
}

void append_visible_rows(const CallHierarchyViewState& view, int node_index,
                         std::vector<int>* rows) {
  if (view.nodes.empty() || node_index < 0 ||
      node_index >= static_cast<int>(view.nodes.size()) || rows == nullptr) {
    return;
  }
  rows->push_back(node_index);
  for (int child : view.nodes[static_cast<std::size_t>(node_index)].children) {
    append_visible_rows(view, child, rows);
  }
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
  const int count = static_cast<int>(view->nodes[0].children.size());
  view->status = view->selected_tab == 0
                     ? std::to_string(count) + " llamada(s) entrante(s)"
                     : std::to_string(count) + " llamada(s) saliente(s)";
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

  const std::vector<CallHierarchyItem> items =
      view->selected_tab == 0 ? symbols->incoming_calls(node.item)
                              : symbols->outgoing_calls(node.item);
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
    assign_node_navigation(&child, item, parent_path, view->selected_tab);
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
  selected_tab = 0;
  selected = 0;
  root_label.clear();
  status.clear();
  nodes.clear();
}

std::vector<int> call_hierarchy_visible_rows(const CallHierarchyViewState& view) {
  std::vector<int> rows;
  if (!view.active || view.nodes.empty()) {
    return rows;
  }
  append_visible_rows(view, 0, &rows);
  return rows;
}

void call_hierarchy_set_tab(CallHierarchyViewState* view, int tab,
                            const std::shared_ptr<ISymbolProvider>& symbols) {
  if (view == nullptr || !view->active || view->nodes.empty() || symbols == nullptr) {
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
    workspace->status_message = "LSP desactivado en configuración";
    return false;
  }
  if (symbols == nullptr || !symbols->supports_call_hierarchy()) {
    workspace->status_message = "Jerarquía de llamadas no disponible (clangd inactivo)";
    return false;
  }

  workspace->ensure_buffer();
  CallHierarchyParams params;
  params.path = workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  params.text = buffer_document_text(workspace->buffer);
  if (params.path.empty()) {
    workspace->status_message = "No hay archivo activo";
    return false;
  }

  int resolved_line = line;
  int resolved_col = col;
  resolve_call_hierarchy_position(symbols, params.path, line, col, symbol_at_cursor,
                                  workspace->buffer, &resolved_line, &resolved_col);
  params.line = resolved_line;
  params.character = resolved_col;

  std::vector<CallHierarchyItem> roots = symbols->prepare_call_hierarchy(params);
  if (roots.empty()) {
    try_prepare_enclosing_call_hierarchy(symbols, &params, line, col, workspace->buffer, &roots,
                                         &resolved_line, &resolved_col);
  }
  if (roots.empty()) {
    workspace->status_message = "Sin jerarquía de llamadas en este ámbito";
    return false;
  }

  view->active = true;
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

  std::string label = view->nodes.front().item.name;
  if (!view->nodes.front().item.detail.empty()) {
    label += " — " + view->nodes.front().item.detail;
  }
  view->root_label = label;

  sidebar->selected_tab = RightSidebarTabs::kCallHierarchy;
  layout_state->right_sidebar.selected_tab = RightSidebarTabs::kCallHierarchy;
  layout_state->right_panel_active_section = 0;
  layout_state->text_input_focus = TextInputFocus::None;
  if (layout_state != nullptr) {
    layout_state->focus_sync_needed = true;
    layout_state->request_ui_tick = true;
  }
  workspace->status_message = "Jerarquía de llamadas: " + label;
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
  if (layout_state != nullptr && layout_state->editor_visible_line_count) {
    visible_lines = std::max(1, layout_state->editor_visible_line_count());
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(path, line, character);
  ensure_scroll_centered(&workspace->buffer, visible_lines);
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
}

}  // namespace tgdb
