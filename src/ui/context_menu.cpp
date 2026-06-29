#include "ui/context_menu.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "editor/text_ops.hpp"
#include "editor/text_search.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/clickable.hpp"
#include "ui/editor_panel.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

constexpr int kMenuOffsetX = 1;
constexpr int kMenuOffsetY = 1;

std::string trim_copy(const std::string& value) {
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return value.substr(start, end - start);
}

bool is_valid_filename(const std::string& name) {
  if (name.empty() || name == "." || name == "..") {
    return false;
  }
  return name.find('/') == std::string::npos && name.find('\\') == std::string::npos;
}

void set_items(ContextMenuState* state, ContextMenuKind kind,
               std::initializer_list<std::pair<const char*, const char*>> items) {
  if (state == nullptr) {
    return;
  }
  state->kind = kind;
  state->labels.clear();
  state->action_ids.clear();
  state->row_boxes.clear();
  state->selected = 0;
  state->delete_confirm_open = false;
  for (const auto& item : items) {
    state->labels.push_back(item.first);
    state->action_ids.push_back(item.second);
    state->row_boxes.push_back(Box{});
  }
}

NavigationParams navigation_params_at(WorkspaceModel* workspace, int line, int col) {
  NavigationParams params;
  if (workspace == nullptr) {
    return params;
  }
  params.path = workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  for (const auto& ln : workspace->buffer.lines) {
    params.text += ln;
    params.text.push_back('\n');
  }
  if (!params.text.empty()) {
    params.text.pop_back();
  }
  params.line = line;
  params.character = col;
  return params;
}

bool navigate_to_location(WorkspaceModel* workspace, MainLayoutState* layout_state,
                          const SourceLocation& loc, int visible_lines) {
  if (workspace == nullptr || !loc.valid || loc.path.empty()) {
    return false;
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(loc.path, loc.line, loc.character);
  workspace->status_message =
      "→ " + fs::path(loc.path).filename().string() + ":" + std::to_string(loc.line + 1) + ":" +
      std::to_string(loc.character + 1);
  ensure_scroll_visible(&workspace->buffer, visible_lines);
  return true;
}

bool go_to_symbol(WorkspaceModel* workspace, MainLayoutState* layout_state,
                  const std::shared_ptr<ISymbolProvider>& symbols, int line, int col,
                  bool declaration, int visible_lines) {
  if (workspace == nullptr || symbols == nullptr || !symbols->supports_navigation()) {
    return false;
  }
  const NavigationParams params = navigation_params_at(workspace, line, col);
  if (params.path.empty()) {
    return false;
  }
  SourceLocation loc =
      declaration ? symbols->goto_declaration(params) : symbols->goto_definition(params);
  if (!loc.valid && !declaration) {
    loc = symbols->goto_declaration(params);
  }
  if (!loc.valid) {
    workspace->status_message = declaration ? "Sin declaración LSP" : "Sin definición LSP";
    return false;
  }
  flash_symbol_at_buffer_pos(workspace, layout_state, line, col, visible_lines);
  schedule_editor_navigation(layout_state, loc);
  return true;
}

void rename_identifier_in_buffer(EditorBuffer* buffer, const std::string& old_name,
                                 const std::string& new_name) {
  if (buffer == nullptr || old_name.empty() || old_name == new_name) {
    return;
  }
  for (std::size_t line_idx = 0; line_idx < buffer->lines.size(); ++line_idx) {
    std::string& line = buffer->lines[line_idx];
    std::size_t pos = 0;
    while (pos <= line.size()) {
      const auto found = line.find(old_name, pos);
      if (found == std::string::npos) {
        break;
      }
      const bool start_ok = found == 0 || !is_ident_char(line[found - 1]);
      const bool end_ok = found + old_name.size() >= line.size() ||
                          !is_ident_char(line[found + old_name.size()]);
      if (start_ok && end_ok) {
        line.replace(found, old_name.size(), new_name);
        pos = found + new_name.size();
        buffer->dirty = true;
      } else {
        pos = found + 1;
      }
    }
  }
  buffer->view_token++;
}

void close_tabs_for_path(WorkspaceModel* workspace, const std::string& absolute_path) {
  if (workspace == nullptr) {
    return;
  }
  const std::string path = normalize_path(absolute_path);
  for (int i = static_cast<int>(workspace->tabs.size()) - 1; i >= 0; --i) {
    if (normalize_path(workspace->tabs[static_cast<std::size_t>(i)].path) == path) {
      workspace->close_tab(i);
    }
  }
}

void focus_search_with_filter(MainLayoutState* layout_state, const std::string& query,
                              const std::string& path_filter) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->right_sidebar.selected_tab = 1;
  layout_state->right_panel_active_section = 0;
  layout_state->right_sidebar.pending_search_setup = true;
  layout_state->right_sidebar.pending_search_query = query;
  layout_state->right_sidebar.pending_search_path_filter = path_filter;
  layout_state->right_sidebar.pending_focus_search = true;
  layout_state->text_input_focus = TextInputFocus::SearchQuery;
  layout_state->request_ui_tick = true;
}

bool delete_path(WorkspaceModel* workspace, DebugModel* model, WorkspaceIndexer* indexer,
                 SymbolWorkspaceIndexer* symbol_indexer, const std::string& absolute_path,
                 const std::string& relative_path, bool is_dir) {
  if (absolute_path.empty() || model == nullptr) {
    return false;
  }
  std::error_code ec;
  const bool ok = is_dir ? fs::remove_all(absolute_path, ec) > 0 : fs::remove(absolute_path, ec);
  if (!ok || ec) {
    if (workspace != nullptr) {
      workspace->status_message = "No se pudo borrar: " + absolute_path;
    }
    return false;
  }
  close_tabs_for_path(workspace, absolute_path);
  if (indexer != nullptr && !relative_path.empty()) {
    if (is_dir) {
      if (auto snap = indexer->snapshot()) {
        for (const auto& file : snap->files) {
          if (file.rfind(relative_path, 0) == 0) {
            indexer->remove_file(model->workspace_root, file);
            if (symbol_indexer != nullptr) {
              symbol_indexer->remove_file(model->workspace_root, file);
            }
          }
        }
      }
    } else {
      indexer->remove_file(model->workspace_root, relative_path);
      if (symbol_indexer != nullptr) {
        symbol_indexer->remove_file(model->workspace_root, relative_path);
      }
    }
  }
  if (workspace != nullptr) {
    workspace->status_message = "Borrado: " + fs::path(absolute_path).filename().string();
  }
  return true;
}

bool rename_path(WorkspaceModel* workspace, DebugModel* model, WorkspaceIndexer* indexer,
                 SymbolWorkspaceIndexer* symbol_indexer, const std::string& absolute_path,
                 const std::string& relative_path, const std::string& new_name, bool is_dir) {
  if (absolute_path.empty() || new_name.empty() || model == nullptr) {
    return false;
  }
  const fs::path parent = fs::path(absolute_path).parent_path();
  const fs::path target = parent / new_name;
  if (target == fs::path(absolute_path)) {
    return true;
  }
  if (fs::exists(target)) {
    if (workspace != nullptr) {
      workspace->status_message = "Ya existe: " + new_name;
    }
    return false;
  }
  std::error_code ec;
  fs::rename(absolute_path, target, ec);
  if (ec) {
    if (workspace != nullptr) {
      workspace->status_message = "No se pudo renombrar: " + ec.message();
    }
    return false;
  }

  const std::string new_relative =
      relative_path.empty()
          ? new_name
          : (fs::path(relative_path).parent_path() / new_name).generic_string();

  if (indexer != nullptr && !relative_path.empty()) {
    if (is_dir) {
      if (auto snap = indexer->snapshot()) {
        for (const auto& file : snap->files) {
          if (file.rfind(relative_path, 0) == 0) {
            const std::string suffix = file.substr(relative_path.size());
            const std::string updated = new_relative + suffix;
            const fs::path updated_abs = fs::path(model->workspace_root) / updated;
            indexer->remove_file(model->workspace_root, file);
            indexer->upsert_file(model->workspace_root, updated, updated_abs.string());
            if (symbol_indexer != nullptr) {
              symbol_indexer->remove_file(model->workspace_root, file);
              symbol_indexer->reindex_file(model->workspace_root, updated, updated_abs.string());
            }
          }
        }
      }
    } else {
      indexer->remove_file(model->workspace_root, relative_path);
      indexer->upsert_file(model->workspace_root, new_relative, target.string());
      if (symbol_indexer != nullptr) {
        symbol_indexer->remove_file(model->workspace_root, relative_path);
        symbol_indexer->reindex_file(model->workspace_root, new_relative, target.string());
      }
    }
  }

  if (workspace != nullptr) {
    workspace->flush_active_tab();
    for (auto& tab : workspace->tabs) {
      if (normalize_path(tab.path) == normalize_path(absolute_path)) {
        tab.path = target.string();
        tab.buffer.path = target.string();
      }
    }
    if (normalize_path(workspace->buffer.path) == normalize_path(absolute_path)) {
      workspace->buffer.path = target.string();
      workspace->active_file = target.string();
    }
    workspace->status_message = "Renombrado: " + new_name;
    workspace->buffer.view_token++;
  }
  return true;
}

void open_rename_prompt(ContextMenuState* state, const std::string& initial) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->delete_confirm_open = false;
  state->rename_open = true;
  state->rename_skip_return = true;
  state->rename_input = initial;
}

void open_delete_confirm(ContextMenuState* state) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->rename_open = false;
  state->delete_confirm_open = true;
}

bool execute_action(ContextMenuState* state, const std::string& action_id,
                    WorkspaceModel* workspace, DebugModel* model, FocusManagerState* focus,
                    MainLayoutState* layout_state, const std::shared_ptr<ISymbolProvider>& symbols,
                    WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                    int editor_visible_lines) {
  if (state == nullptr) {
    return false;
  }

  if (action_id == "open_file") {
    if (workspace != nullptr) {
      workspace->open_file(state->absolute_path);
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  if (action_id == "delete_file") {
    open_delete_confirm(state);
    return true;
  }

  if (action_id == "rename_file") {
    open_rename_prompt(state, fs::path(state->absolute_path).filename().string());
    return true;
  }

  if (action_id == "rename_folder") {
    open_rename_prompt(state, fs::path(state->absolute_path).filename().string());
    return true;
  }

  if (action_id == "search_in_folder") {
    focus_search_with_filter(layout_state, std::string{}, state->relative_path);
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    return true;
  }

  if (action_id == "go_definition") {
    if (workspace != nullptr) {
      workspace->ensure_buffer();
      workspace->buffer.reset_to_single_cursor(state->editor_line, state->editor_col);
    }
    go_to_symbol(workspace, layout_state, symbols, state->editor_line, state->editor_col, false,
                 editor_visible_lines);
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  if (action_id == "go_implementation") {
    if (workspace != nullptr) {
      workspace->ensure_buffer();
      workspace->buffer.reset_to_single_cursor(state->editor_line, state->editor_col);
    }
    go_to_symbol(workspace, layout_state, symbols, state->editor_line, state->editor_col, true,
                 editor_visible_lines);
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  if (action_id == "rename_symbol") {
    open_rename_prompt(state, state->symbol_name);
    return true;
  }

  if (action_id == "find_references") {
    focus_search_with_filter(layout_state, state->symbol_name, std::string{});
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    return true;
  }

  return false;
}

bool commit_rename(ContextMenuState* state, WorkspaceModel* workspace, DebugModel* model,
                   WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer) {
  if (state == nullptr) {
    return false;
  }
  const std::string new_name = trim_copy(state->rename_input);
  if (!is_valid_filename(new_name)) {
    if (workspace != nullptr) {
      workspace->status_message = "Nombre no válido";
    }
    return false;
  }

  if (state->kind == ContextMenuKind::EditorSymbol) {
    if (workspace != nullptr) {
      workspace->ensure_buffer();
      rename_identifier_in_buffer(&workspace->buffer, state->symbol_name, new_name);
      workspace->flush_active_tab();
      workspace->status_message = "Renombrado: " + state->symbol_name + " → " + new_name;
    }
    return true;
  }

  const bool is_dir = state->kind == ContextMenuKind::Folder;
  return rename_path(workspace, model, indexer, symbol_indexer, state->absolute_path,
                     state->relative_path, new_name, is_dir);
}

int row_index_at(const ContextMenuState& state, int x, int y) {
  for (int i = 0; i < static_cast<int>(state.row_boxes.size()); ++i) {
    if (state.row_boxes[static_cast<std::size_t>(i)].Contain(x, y)) {
      return i;
    }
  }
  return -1;
}

}  // namespace

bool context_menu_active(const ContextMenuState* state) {
  return state != nullptr && (state->open || state->rename_open || state->delete_confirm_open);
}

void context_menu_close(ContextMenuState* state, MainLayoutState* layout_state) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->rename_input.clear();
  state->selected = 0;
  state->row_boxes.clear();
  if (layout_state != nullptr) {
    layout_state->clickable.clear_hover_if(press_id::is_context_menu_hover);
    layout_state->request_ui_tick = true;
  }
}

void context_menu_open_file(ContextMenuState* state, int x, int y,
                            const std::string& absolute_path, const std::string& relative_path) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->relative_path = relative_path;
  set_items(state, ContextMenuKind::File,
            {{"Abrir archivo", "open_file"},
             {"Renombrar archivo", "rename_file"},
             {"Borrar archivo", "delete_file"}});
}

void context_menu_open_folder(ContextMenuState* state, int x, int y,
                              const std::string& absolute_path, const std::string& relative_path) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->relative_path = relative_path;
  set_items(state, ContextMenuKind::Folder,
            {{"Renombrar carpeta", "rename_folder"},
             {"Buscar en…", "search_in_folder"}});
}

void context_menu_open_editor_symbol(ContextMenuState* state, int x, int y, int line, int col,
                                     int sym_start, int sym_end, const std::string& symbol) {
  if (state == nullptr || symbol.empty()) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->editor_line = line;
  state->editor_col = col;
  state->editor_sym_start = sym_start;
  state->editor_sym_end = sym_end;
  state->symbol_name = symbol;
  set_items(state, ContextMenuKind::EditorSymbol,
            {{"Ir a definición", "go_definition"},
             {"Ir a implementación", "go_implementation"},
             {"Renombrar", "rename_symbol"},
             {"Encontrar referencias", "find_references"}});
}

Element render_delete_confirm_modal(const ContextMenuState* state) {
  if (state == nullptr || !state->delete_confirm_open) {
    return text("");
  }
  const std::string name = fs::path(state->absolute_path).filename().string();
  Element dialog = ModalWindow(
      text("Confirmar borrado") | color(theme::Accent()),
      vbox({text(" ¿Borrar " + name + "? ") | color(theme::Header()),
            text(" Enter confirmar  Esc cancelar") | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

Element render_rename_modal(ContextMenuState* state) {
  if (state == nullptr || !state->rename_open) {
    return text("");
  }
  std::string line = state->rename_input;
  line.push_back('_');
  const char* title = state->kind == ContextMenuKind::EditorSymbol ? "Renombrar símbolo"
                     : state->kind == ContextMenuKind::Folder     ? "Renombrar carpeta"
                                                                  : "Renombrar archivo";
  Element dialog = ModalWindow(
      text(title) | color(theme::Accent()),
      vbox({ModalInputLine(line),
            text(" Enter confirmar  Esc cancelar") | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

Element render_context_menu_overlay(ContextMenuState* state, MainLayoutState* layout_state,
                                    Element base) {
  if (state == nullptr) {
    return text("");
  }

  if (state->delete_confirm_open) {
    return ScreenModalOverlay(std::move(base), render_delete_confirm_modal(state));
  }

  if (state->rename_open) {
    return ScreenModalOverlay(std::move(base), render_rename_modal(state));
  }

  if (!state->open || state->labels.empty()) {
    return text("");
  }

  Elements rows;
  state->row_boxes.resize(state->labels.size());
  for (int i = 0; i < static_cast<int>(state->labels.size()); ++i) {
    const std::string row_id = press_id::context_menu_row(i);
    const bool hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
    const bool pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
    Element row = text(" " + state->labels[static_cast<std::size_t>(i)] + " ") |
                  color(theme::Header()) | bgcolor(theme::PanelBg());
    row = StyleListRow(std::move(row), i == state->selected, hovered, pressed);
    rows.push_back(row | reflect(state->row_boxes[static_cast<std::size_t>(i)]));
  }

  Element menu = vbox(std::move(rows)) | border | bgcolor(theme::PanelBg()) |
                 reflect(state->menu_box);

  const int x_pad = state->anchor_x + kMenuOffsetX;
  const int y_pad = state->anchor_y + kMenuOffsetY;
  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, x_pad), menu | clear_under, filler()}),
                     filler()}) |
                   flex});
}

bool handle_context_menu_hover(ContextMenuState* state, MainLayoutState* layout_state,
                               const Mouse& m) {
  if (state == nullptr || layout_state == nullptr || !state->open || state->rename_open ||
      state->delete_confirm_open || m.motion != Mouse::Moved) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  if (!state->menu_box.Contain(m.x, m.y)) {
    layout_state->clickable.clear_hover_if(press_id::is_context_menu_hover);
  } else {
    const int row = row_index_at(*state, m.x, m.y);
    if (row >= 0) {
      layout_state->clickable.set_hover(press_id::context_menu_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_context_menu_hover);
    }
  }
  if (layout_state->clickable.hovered_id() != before) {
    layout_state->request_ui_tick = true;
    return true;
  }
  return false;
}

bool handle_context_menu_mouse(ContextMenuState* state, MainLayoutState* layout_state,
                               const Mouse& m, int* clicked_row) {
  if (clicked_row != nullptr) {
    *clicked_row = -1;
  }
  if (state == nullptr || !state->open || state->rename_open || state->delete_confirm_open) {
    return false;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
  }

  if (!state->menu_box.Contain(m.x, m.y)) {
    context_menu_close(state, layout_state);
    return false;
  }

  const int row = row_index_at(*state, m.x, m.y);
  if (row < 0) {
    return true;
  }
  trigger_press(layout_state, press_id::context_menu_row(row));
  state->selected = row;
  if (clicked_row != nullptr) {
    *clicked_row = row;
  }
  return true;
}

bool handle_context_menu_keys(ContextMenuState* state, WorkspaceModel* workspace,
                              DebugModel* model, FocusManagerState* focus,
                              MainLayoutState* layout_state,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                              int editor_visible_lines, const Event& event) {
  if (state == nullptr || !context_menu_active(state)) {
    return false;
  }

  if (state->delete_confirm_open) {
    if (event == Event::Escape) {
      context_menu_close(state, layout_state);
      return true;
    }
    if (event == Event::Return) {
      delete_path(workspace, model, indexer, symbol_indexer, state->absolute_path,
                  state->relative_path, false);
      context_menu_close(state, layout_state);
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      return true;
    }
    return true;
  }

  if (state->rename_open) {
    if (event == Event::Escape) {
      context_menu_close(state, layout_state);
      return true;
    }
    if (event == Event::Return) {
      if (state->rename_skip_return) {
        state->rename_skip_return = false;
        return true;
      }
      if (commit_rename(state, workspace, model, indexer, symbol_indexer)) {
        context_menu_close(state, layout_state);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      }
      return true;
    }
    if (event == Event::Backspace) {
      state->rename_skip_return = false;
      if (!state->rename_input.empty()) {
        state->rename_input.pop_back();
      }
      return true;
    }
    if (event.is_character()) {
      state->rename_skip_return = false;
      const std::string ch = event.character();
      if (ch.size() == 1 && std::isprint(static_cast<unsigned char>(ch[0]))) {
        state->rename_input.push_back(ch[0]);
      }
      return true;
    }
    return true;
  }

  if (event == Event::Escape) {
    context_menu_close(state, layout_state);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->selected = std::min(state->selected + 1, static_cast<int>(state->labels.size()) - 1);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->selected = std::max(0, state->selected - 1);
    return true;
  }
  if (event == Event::Return) {
    if (state->selected >= 0 && state->selected < static_cast<int>(state->action_ids.size())) {
      trigger_press(layout_state, press_id::context_menu_row(state->selected));
      execute_action(state, state->action_ids[static_cast<std::size_t>(state->selected)], workspace,
                     model, focus, layout_state, symbols, indexer, symbol_indexer,
                     editor_visible_lines);
      if (!state->rename_open && !state->delete_confirm_open) {
        context_menu_close(state, layout_state);
      }
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
    }
    return true;
  }
  return true;
}

Component MakeContextMenuOverlay(Component main, ContextMenuState* state, WorkspaceModel* workspace,
                                 DebugModel* model, FocusManagerState* focus,
                                 MainLayoutState* layout_state,
                                 const std::shared_ptr<ISymbolProvider>& symbols,
                                 WorkspaceIndexer* indexer,
                                 SymbolWorkspaceIndexer* symbol_indexer,
                                 std::function<int()> editor_visible_lines) {
  auto run_selected_action = [=](int row) {
    if (state == nullptr || row < 0 || row >= static_cast<int>(state->action_ids.size())) {
      return;
    }
    const int visible = editor_visible_lines ? editor_visible_lines() : 24;
    execute_action(state, state->action_ids[static_cast<std::size_t>(row)], workspace, model,
                   focus, layout_state, symbols, indexer, symbol_indexer, visible);
    if (!state->rename_open && !state->delete_confirm_open) {
      context_menu_close(state, layout_state);
    }
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
  };

  auto handler = [=](Event event) {
    if (state == nullptr || !context_menu_active(state)) {
      return false;
    }
    if (event.is_mouse()) {
      if (event.mouse().motion == Mouse::Moved) {
        handle_context_menu_hover(state, layout_state, event.mouse());
        return true;
      }
      int clicked_row = -1;
      if (handle_context_menu_mouse(state, layout_state, event.mouse(), &clicked_row)) {
        if (clicked_row >= 0) {
          run_selected_action(clicked_row);
        }
        return true;
      }
    }
    const int visible = editor_visible_lines ? editor_visible_lines() : 24;
    return handle_context_menu_keys(state, workspace, model, focus, layout_state, symbols, indexer,
                                    symbol_indexer, visible, event);
  };

  return Renderer(CatchEvent(main, handler),
                  [main, state, layout_state] {
                    Element base = main->Render();
                    if (state == nullptr || !context_menu_active(state)) {
                      return base;
                    }
                    Element overlay = render_context_menu_overlay(state, layout_state, base);
                    if (state->rename_open || state->delete_confirm_open) {
                      return overlay;
                    }
                    return dbox({std::move(base), std::move(overlay)});
                  });
}

}  // namespace tgdb
