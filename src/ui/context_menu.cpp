#include "ui/binary_symbols_panel.hpp"
#include "ui/call_hierarchy_view.hpp"
#include "ui/context_menu.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "editor/text_ops.hpp"
#include "editor/text_search.hpp"
#include "editor/undo_stack.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "indexer/index_rules.hpp"
#include "lsp/lsp_text_edits.hpp"
#include "symbols/code_action.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/editor_panel.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/compile_commands_lookup.hpp"
#include "util/clang_format_config.hpp"
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

bool path_is_same_or_descendant(const fs::path& ancestor, const fs::path& path) {
  std::error_code ec;
  const auto a = fs::weakly_canonical(ancestor, ec);
  if (ec) {
    return false;
  }
  const auto p = fs::weakly_canonical(path, ec);
  if (ec) {
    return false;
  }
  const std::string a_str = a.string();
  const std::string p_str = p.string();
  if (a_str == p_str) {
    return true;
  }
  if (p_str.size() <= a_str.size()) {
    return false;
  }
  if (p_str.compare(0, a_str.size(), a_str) != 0) {
    return false;
  }
  const char next = p_str[a_str.size()];
  return next == '/' || next == '\\';
}

void set_items(ContextMenuState* state, ContextMenuKind kind,
               std::initializer_list<std::pair<std::string, const char*>> items) {
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

bool debug_watches_available(const DebugModel* model) {
  return model != nullptr && model->state != DebugState::kDisconnected &&
         model->state != DebugState::kTerminated;
}

bool hardware_watch_available(const DebugModel* model) {
  return debug_watches_available(model) && !model->is_post_mortem &&
         model->state == DebugState::kStopped;
}

void assign_watch_frame(UiCommand* command, const DebugModel* model) {
  if (command == nullptr || model == nullptr) {
    return;
  }
  if (model->variables_frame_id >= 0) {
    command->frame_id = model->variables_frame_id;
  } else if (!model->stack_frames.empty()) {
    command->frame_id = model->stack_frames[model->selected_frame].id;
  }
}

void append_debug_watch_items(ContextMenuState* state, bool show_hardware_watch) {
  if (state == nullptr) {
    return;
  }
  state->labels.push_back(i18n::tr("context_menu.add_to_watch"));
  state->action_ids.push_back("add_to_watch");
  state->row_boxes.push_back(Box{});
  if (show_hardware_watch) {
    state->labels.push_back(i18n::tr("context_menu.hardware_watch"));
    state->action_ids.push_back("hardware_watch");
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

// See editor_panel.cpp's buffer_text(): unified onto the cached,
// backend-agnostic-O(n) editor_buffer_joined_source().
std::string buffer_document_text(const EditorBuffer& buffer) {
  return editor_buffer_joined_source(buffer);
}

bool read_file_text(const std::string& absolute_path, std::string* text_out);
bool write_file_text(const std::string& absolute_path, const std::string& text);

void apply_document_text_to_buffer(EditorBuffer* buffer, const std::string& text) {
  if (buffer == nullptr) {
    return;
  }
  buffer->lines.assign(lines_from_document_text(text));
  if (buffer->lines.empty()) {
    buffer->lines.push_back("");
  }
  buffer->reset_to_single_cursor(0, 0);
  buffer->dirty = true;
  clear_undo(buffer);
  buffer->view_token++;
}

void reload_buffer_text(EditorBuffer* buffer, const std::string& text, int line, int col) {
  if (buffer == nullptr) {
    return;
  }
  buffer->lines.assign(lines_from_document_text(text));
  if (buffer->lines.empty()) {
    buffer->lines.push_back("");
  }
  line = std::max(0, std::min(line, static_cast<int>(buffer->lines.size()) - 1));
  col = std::max(0, std::min(col, static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size())));
  buffer->reset_to_single_cursor(line, col);
  buffer->dirty = true;
  buffer->view_token++;
}

bool apply_workspace_file_edits(WorkspaceModel* workspace, DebugModel* model,
                                const std::shared_ptr<ISymbolProvider>& symbols,
                                SymbolWorkspaceIndexer* symbol_indexer,
                                const std::vector<LspFileEdits>& file_edits,
                                const std::string& navigation_path, int navigation_line,
                                int navigation_col, std::string* status_message) {
  if (workspace == nullptr || file_edits.empty()) {
    return false;
  }

  workspace->flush_active_tab();
  int changed_files = 0;
  for (const LspFileEdits& file_edit : file_edits) {
    const std::string path = normalize_path(file_edit.path);
    const int tab = workspace->find_tab(path);

    std::string text;
    if (tab >= 0) {
      text = buffer_document_text(workspace->tabs[static_cast<std::size_t>(tab)].buffer);
    } else if (!read_file_text(path, &text)) {
      if (status_message != nullptr) {
        *status_message = i18n::tr_fmt("status.read_failed",
                                       {fs::path(path).filename().string()});
      }
      return false;
    }

    const std::string updated = apply_lsp_text_edits(text, file_edit.edits);
    if (updated == text) {
      continue;
    }

    if (tab >= 0) {
      EditorBuffer& tab_buffer = workspace->tabs[static_cast<std::size_t>(tab)].buffer;
      push_undo(&tab_buffer);
      const int target_line = normalize_path(path) == normalize_path(navigation_path)
                                  ? navigation_line
                                  : tab_buffer.primary_line();
      const int target_col = normalize_path(path) == normalize_path(navigation_path)
                                 ? navigation_col
                                 : tab_buffer.primary_col();
      reload_buffer_text(&tab_buffer, updated, target_line, target_col);
      if (symbols != nullptr) {
        symbols->on_document_changed(path, updated);
        symbols->flush_document_sync(path);
      }
    } else if (!write_file_text(path, updated)) {
      if (status_message != nullptr) {
        *status_message = i18n::tr_fmt("status.save_failed",
                                       {fs::path(path).filename().string()});
      }
      return false;
    }

    if (symbol_indexer != nullptr && model != nullptr && !model->workspace_root.empty()) {
      std::error_code ec;
      const auto relative = fs::relative(path, model->workspace_root, ec);
      if (!ec) {
        symbol_indexer->reindex_file(model->workspace_root, relative.generic_string(), path);
      }
    }
    ++changed_files;
  }

  if (changed_files == 0) {
    if (status_message != nullptr) {
      *status_message = i18n::tr("status.no_changes_apply");
    }
    return false;
  }

  if (workspace->active_tab >= 0) {
    workspace->load_active_tab_into_buffer();
  }
  return true;
}

bool read_file_text(const std::string& absolute_path, std::string* text_out) {
  if (text_out == nullptr || absolute_path.empty()) {
    return false;
  }
  std::ifstream input(absolute_path);
  if (!input) {
    return false;
  }
  std::ostringstream ss;
  ss << input.rdbuf();
  *text_out = ss.str();
  return true;
}

bool write_file_text(const std::string& absolute_path, const std::string& text) {
  if (absolute_path.empty()) {
    return false;
  }
  std::ofstream output(absolute_path, std::ios::trunc | std::ios::binary);
  if (!output) {
    return false;
  }
  output << text;
  return static_cast<bool>(output);
}

bool format_file_at_path(WorkspaceModel* workspace, MainLayoutState* layout_state,
                         const std::shared_ptr<ISymbolProvider>& symbols,
                         const std::string& absolute_path) {
  if (workspace == nullptr || absolute_path.empty()) {
    return false;
  }
  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    workspace->status_message = i18n::tr("status.lsp_disabled");
    return false;
  }
  if (symbols == nullptr || !symbols->supports_formatting()) {
    workspace->status_message = i18n::tr("status.format_unavailable");
    return false;
  }
  if (!is_lsp_trackable_path(absolute_path)) {
    workspace->status_message = i18n::tr("status.format_incompatible");
    return false;
  }

  if (!workspace->root.empty()) {
    const ClangFormatConfig* active =
        layout_state != nullptr ? layout_state->workspace_clang_format : nullptr;
    sync_clang_format_file_for_formatting(workspace->root, active);
  } else {
    const std::string format_root = clang_format_root_for_file(absolute_path);
    if (!format_root.empty()) {
      save_clang_format(format_root, load_clang_format_from_disk(format_root));
    }
  }

  workspace->flush_active_tab();
  const std::string path = normalize_path(absolute_path);
  const int tab = workspace->find_tab(path);

  std::string text;
  if (tab >= 0) {
    text = buffer_document_text(workspace->tabs[static_cast<std::size_t>(tab)].buffer);
  } else if (!read_file_text(path, &text)) {
    workspace->status_message = i18n::tr_fmt("status.read_failed",
                                             {fs::path(path).filename().string()});
    return false;
  }

  const std::optional<std::string> formatted =
      symbols->format_document(FormatParams{path, text});
  if (!formatted.has_value()) {
    workspace->status_message = i18n::tr("status.format_error");
    return false;
  }
  if (*formatted == text) {
    workspace->status_message =
        i18n::tr_fmt("status.format_no_changes", {fs::path(path).filename().string()});
    return true;
  }

  if (tab >= 0) {
    EditorBuffer& tab_buffer = workspace->tabs[static_cast<std::size_t>(tab)].buffer;
    apply_document_text_to_buffer(&tab_buffer, *formatted);
    if (tab == workspace->active_tab) {
      workspace->load_active_tab_into_buffer();
    }
    symbols->on_document_changed(path, *formatted);
    symbols->flush_document_sync(path);
  } else if (!write_file_text(path, *formatted)) {
    workspace->status_message = i18n::tr_fmt("status.save_failed",
                                             {fs::path(path).filename().string()});
    return false;
  }

  workspace->status_message = i18n::tr_fmt("status.formatted", {fs::path(path).filename().string()});
  return true;
}

bool navigate_to_location(WorkspaceModel* workspace, MainLayoutState* layout_state,
                          const SourceLocation& loc, int visible_lines) {
  if (workspace == nullptr || !loc.valid || loc.path.empty()) {
    return false;
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(loc.path, loc.line, loc.character);
  workspace->status_message =
      i18n::tr_fmt("status.navigate",
                   {fs::path(loc.path).filename().string(), std::to_string(loc.line + 1),
                    std::to_string(loc.character + 1)});
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
  SourceLocation loc = resolve_symbol_navigation(*symbols, params, declaration);
  if (!loc.valid) {
    workspace->status_message =
        declaration ? i18n::tr("status.no_declaration") : i18n::tr("status.no_definition");
    return false;
  }
  flash_symbol_at_buffer_pos(workspace, layout_state, line, col, visible_lines);
  apply_editor_navigation(layout_state, loc, [&](const SourceLocation& target) {
    navigate_to_location(workspace, layout_state, target, visible_lines);
  });
  return true;
}

bool go_to_implementation(WorkspaceModel* workspace, MainLayoutState* layout_state,
                          const std::shared_ptr<ISymbolProvider>& symbols, int line, int col,
                          int visible_lines) {
  if (workspace == nullptr || symbols == nullptr || !symbols->supports_navigation()) {
    return false;
  }
  const NavigationParams params = navigation_params_at(workspace, line, col);
  if (params.path.empty()) {
    return false;
  }
  SourceLocation loc = resolve_implementation_navigation(*symbols, params);
  if (!loc.valid || navigation_at_same_spot(loc, params)) {
    workspace->status_message = i18n::tr("status.no_definition");
    return false;
  }
  flash_symbol_at_buffer_pos(workspace, layout_state, line, col, visible_lines);
  apply_editor_navigation(layout_state, loc, [&](const SourceLocation& target) {
    navigate_to_location(workspace, layout_state, target, visible_lines);
  });
  return true;
}

bool rename_symbol_with_lsp(ContextMenuState* state, WorkspaceModel* workspace,
                            MainLayoutState* layout_state,
                            const std::shared_ptr<ISymbolProvider>& symbols, DebugModel* model,
                            SymbolWorkspaceIndexer* symbol_indexer, const std::string& new_name) {
  if (state == nullptr || workspace == nullptr) {
    return false;
  }
  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    workspace->status_message = i18n::tr("status.lsp_disabled");
    return false;
  }
  if (symbols == nullptr || !symbols->supports_rename()) {
    workspace->status_message = i18n::tr("status.rename_unavailable");
    return false;
  }

  workspace->ensure_buffer();
  const NavigationParams nav =
      navigation_params_at(workspace, state->editor_line, state->editor_col);
  if (nav.path.empty()) {
    workspace->status_message = i18n::tr("status.no_active_file_rename");
    return false;
  }

  RenameParams params;
  params.path = nav.path;
  params.text = nav.text;
  params.line = state->editor_line;
  params.character = state->editor_col;
  params.new_name = new_name;

  const std::vector<LspFileEdits> file_edits = symbols->rename_symbol(params);
  if (file_edits.empty()) {
    workspace->status_message = i18n::tr("status.rename_failed");
    return false;
  }

  std::string status;
  if (!apply_workspace_file_edits(workspace, model, symbols, symbol_indexer, file_edits, nav.path,
                                  state->editor_line, state->editor_col, &status)) {
    workspace->status_message =
        status.empty() ? i18n::tr("status.rename_failed") : status;
    return false;
  }

  workspace->status_message = i18n::tr_fmt("status.renamed",
                                           {state->symbol_name, new_name,
                                            std::to_string(file_edits.size())});
  return true;
}

bool apply_problem_quick_fix(ContextMenuState* state, WorkspaceModel* workspace,
                             DebugModel* model, MainLayoutState* layout_state,
                             const std::shared_ptr<ISymbolProvider>& symbols,
                             SymbolWorkspaceIndexer* symbol_indexer) {
  if (state == nullptr || workspace == nullptr) {
    return false;
  }
  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    workspace->status_message = i18n::tr("status.lsp_disabled");
    return false;
  }
  if (symbols == nullptr || !symbols->supports_code_actions()) {
    workspace->status_message = i18n::tr("status.fix_unavailable");
    return false;
  }

  const std::string path = state->problem_path;
  if (path.empty()) {
    workspace->status_message = i18n::tr("status.no_file_for_problem");
    return false;
  }

  std::string text;
  const int tab = workspace->find_tab(path);
  if (tab >= 0) {
    text = buffer_document_text(workspace->tabs[static_cast<std::size_t>(tab)].buffer);
  } else if (!read_file_text(path, &text)) {
    workspace->status_message = i18n::tr_fmt("status.read_failed",
                                             {fs::path(path).filename().string()});
    return false;
  }

  CodeActionParams params;
  params.path = path;
  params.text = text;
  params.line = state->problem_line;
  params.start_col = state->problem_start_col;
  params.end_col = state->problem_end_col;
  params.diagnostic.line = state->problem_line;
  params.diagnostic.start_col = state->problem_start_col;
  params.diagnostic.end_col = state->problem_end_col;
  params.diagnostic.message = state->problem_message;
  params.diagnostic.source = "clang";
  params.diagnostic.severity = DiagnosticSeverity::kError;

  const std::vector<CodeActionItem> actions = symbols->code_actions_for_diagnostic(params);
  const CodeActionItem* chosen = nullptr;
  for (const CodeActionItem& action : actions) {
    if (!action.file_edits.empty()) {
      chosen = &action;
      break;
    }
  }
  if (chosen == nullptr) {
    workspace->status_message = i18n::tr("status.no_fix_available");
    return false;
  }

  std::string status;
  if (!apply_workspace_file_edits(workspace, model, symbols, symbol_indexer, chosen->file_edits, path,
                                  state->problem_line, state->problem_start_col, &status)) {
    workspace->status_message = status.empty() ? i18n::tr("status.fix_apply_failed") : status;
    return false;
  }

  workspace->status_message =
      i18n::tr_fmt("status.fix_applied",
                   {chosen->title.empty() ? i18n::tr("status.fix_quickfix") : chosen->title});
  return true;
}

void close_tabs_for_path(WorkspaceModel* workspace, const std::string& absolute_path,
                         bool is_dir) {
  if (workspace == nullptr) {
    return;
  }
  const std::string path = normalize_path(absolute_path);
  const std::string prefix = path.empty() || path.back() == '/' ? path : path + "/";
  for (int i = static_cast<int>(workspace->tabs.size()) - 1; i >= 0; --i) {
    const std::string tab_path =
        normalize_path(workspace->tabs[static_cast<std::size_t>(i)].path);
    if (tab_path == path || (is_dir && tab_path.rfind(prefix, 0) == 0)) {
      workspace->close_tab(i);
    }
  }
}

void focus_call_hierarchy(MainLayoutState* layout_state, int line, int col,
                          const std::string& symbol) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->console_visible = true;
  layout_state->console_tabs.selected_tab = ConsolePanelTabs::kCallHierarchy;
  layout_state->right_panel_active_section = 0;
  layout_state->right_sidebar.pending_call_hierarchy = true;
  layout_state->right_sidebar.pending_call_hierarchy_line = line;
  layout_state->right_sidebar.pending_call_hierarchy_col = col;
  layout_state->right_sidebar.pending_call_hierarchy_symbol = symbol;
  layout_state->text_input_focus = TextInputFocus::None;
  layout_state->focus_sync_needed = true;
  UI_WAKE(layout_state, "wake");
}

bool delete_path(WorkspaceModel* workspace, DebugModel* model, WorkspaceIndexer* indexer,
                 SymbolWorkspaceIndexer* symbol_indexer, const std::string& absolute_path,
                 const std::string& relative_path, bool is_dir) {
  if (absolute_path.empty() || model == nullptr) {
    return false;
  }
  if (is_dir && relative_path.empty()) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr("status.delete_workspace_root");
    }
    return false;
  }
  std::error_code ec;
  const bool ok = is_dir ? fs::remove_all(absolute_path, ec) > 0 : fs::remove(absolute_path, ec);
  if (!ok || ec) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.delete_failed", {absolute_path});
    }
    return false;
  }
  close_tabs_for_path(workspace, absolute_path, is_dir);
  if (indexer != nullptr && !relative_path.empty()) {
    if (is_dir) {
      indexer->remove_path_prefix(model->workspace_root, relative_path);
      if (symbol_indexer != nullptr) {
        symbol_indexer->remove_path_prefix(model->workspace_root, relative_path);
      }
    } else {
      indexer->remove_file(model->workspace_root, relative_path);
      if (symbol_indexer != nullptr) {
        symbol_indexer->remove_file(model->workspace_root, relative_path);
      }
    }
  }
  if (workspace != nullptr) {
    workspace->status_message =
        i18n::tr_fmt("status.deleted", {fs::path(absolute_path).filename().string()});
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
      workspace->status_message = i18n::tr_fmt("status.already_exists", {new_name});
    }
    return false;
  }
  std::error_code ec;
  fs::rename(absolute_path, target, ec);
  if (ec) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.rename_file_failed", {ec.message()});
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
    workspace->status_message = i18n::tr_fmt("status.renamed_file", {new_name});
    workspace->buffer.view_token++;
  }
  return true;
}

bool create_path(WorkspaceModel* workspace, DebugModel* model, WorkspaceIndexer* indexer,
                 SymbolWorkspaceIndexer* symbol_indexer, const std::string& parent_absolute,
                 const std::string& parent_relative, const std::string& name, bool is_folder) {
  if (parent_absolute.empty() || name.empty() || model == nullptr) {
    return false;
  }
  const fs::path target = fs::path(parent_absolute) / name;
  if (fs::exists(target)) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.already_exists", {name});
    }
    return false;
  }

  std::error_code ec;
  if (is_folder) {
    if (!fs::create_directory(target, ec) || ec) {
      if (workspace != nullptr) {
        workspace->status_message = i18n::tr_fmt("status.create_failed", {name});
      }
      return false;
    }
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.created_folder", {name});
    }
    return true;
  }

  std::ofstream out(target);
  if (!out) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.create_failed", {name});
    }
    return false;
  }

  const std::string new_relative =
      parent_relative.empty() ? name : parent_relative + "/" + name;
  if (indexer != nullptr) {
    indexer->upsert_file(model->workspace_root, new_relative, target.string());
    if (symbol_indexer != nullptr) {
      symbol_indexer->reindex_file(model->workspace_root, new_relative, target.string());
    }
  }
  if (workspace != nullptr) {
    workspace->status_message = i18n::tr_fmt("status.created_file", {name});
  }
  return true;
}

bool move_path(WorkspaceModel* workspace, DebugModel* model, WorkspaceIndexer* indexer,
               SymbolWorkspaceIndexer* symbol_indexer, const std::string& absolute_path,
               const std::string& relative_path, const std::string& dest_dir, bool is_dir) {
  if (absolute_path.empty() || dest_dir.empty() || model == nullptr) {
    return false;
  }
  const fs::path source(absolute_path);
  const fs::path target = fs::path(dest_dir) / source.filename();
  if (target == source) {
    return true;
  }
  if (is_dir && path_is_same_or_descendant(source, fs::path(dest_dir))) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr("status.cannot_move_into_self");
    }
    return false;
  }
  if (fs::exists(target)) {
    if (workspace != nullptr) {
      workspace->status_message =
          i18n::tr_fmt("status.already_exists", {target.filename().string()});
    }
    return false;
  }

  std::error_code ec;
  fs::rename(source, target, ec);
  if (ec) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr_fmt("status.move_failed", {ec.message()});
    }
    return false;
  }

  std::string new_relative;
  const auto rel = fs::relative(target, fs::path(model->workspace_root), ec);
  if (!ec) {
    new_relative = rel.generic_string();
  } else if (!relative_path.empty()) {
    const std::string filename = source.filename().string();
    std::error_code rel_ec;
    const auto dest_rel =
        fs::relative(fs::path(dest_dir), fs::path(model->workspace_root), rel_ec);
    new_relative = rel_ec ? filename : (dest_rel / filename).generic_string();
  } else {
    new_relative = target.filename().string();
  }

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
    workspace->status_message = i18n::tr_fmt("status.moved", {source.filename().string()});
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
  state->name_prompt_kind = NamePromptKind::Rename;
}

void open_create_prompt(ContextMenuState* state, bool is_folder) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->delete_confirm_open = false;
  state->rename_open = true;
  state->rename_skip_return = true;
  state->rename_input.clear();
  state->name_prompt_kind =
      is_folder ? NamePromptKind::CreateFolder : NamePromptKind::CreateFile;
}

void open_move_browser(ContextMenuState* state, DebugModel* model) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->move_browser_open = true;
  state->move_is_dir = state->kind == ContextMenuKind::Folder;
  const std::string root = model != nullptr ? model->workspace_root : "";
  state->move_browser.launch_root =
      root.empty() ? canonical_browser_root("") : canonical_browser_root(root);
  std::string start = state->move_browser.launch_root;
  if (!state->absolute_path.empty()) {
    const fs::path parent = fs::path(state->absolute_path).parent_path();
    if (!parent.empty()) {
      start = parent.string();
    }
  }
  state->move_browser.reset(start);
}

void open_indexer_paths_modal(ContextMenuState* state, const std::string& workspace_root,
                              const WorkspaceConfig* workspace_config) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->indexer_paths_open = true;
  state->indexer_paths_scroll = 0;
  state->indexer_paths_lines.clear();
  if (workspace_config != nullptr && !workspace_root.empty()) {
    const FileIndexerPaths paths =
        lookup_file_indexer_paths(workspace_root, *workspace_config, state->absolute_path);
    state->indexer_paths_lines = paths.display_lines;
  } else {
    state->indexer_paths_lines.push_back(i18n::tr("context_menu.indexer_paths.section"));
    state->indexer_paths_lines.push_back(i18n::tr("context_menu.indexer_paths.no_workspace"));
  }
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
                    WorkspaceModel* workspace, WorkspaceModel* secondary_workspace,
                    DebugModel* model, FocusManagerState* focus, MainLayoutState* layout_state,
                    const std::shared_ptr<ISymbolProvider>& symbols, WorkspaceIndexer* indexer,
                    SymbolWorkspaceIndexer* symbol_indexer, const WorkspaceConfig* workspace_config,
                    int editor_visible_lines, CommandCallback on_command) {
  if (state == nullptr) {
    return false;
  }

  if (action_id == "open_file_secondary") {
    if (secondary_workspace != nullptr) {
      secondary_workspace->open_file(state->absolute_path);
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::SecondaryEditor;
    }
    return true;
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

  if (action_id == "analyze_symbols") {
    request_binary_symbols_panel(layout_state, state->absolute_path);
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
    return true;
  }

  if (action_id == "show_indexer_paths") {
    const std::string workspace_root =
        model != nullptr ? model->workspace_root : (workspace != nullptr ? workspace->root : "");
    open_indexer_paths_modal(state, workspace_root, workspace_config);
    return true;
  }

  if (action_id == "delete_file" || action_id == "delete_folder") {
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

  if (action_id == "create_folder") {
    open_create_prompt(state, true);
    return true;
  }

  if (action_id == "create_file") {
    open_create_prompt(state, false);
    return true;
  }

  if (action_id == "move_to") {
    open_move_browser(state, model);
    return true;
  }

  if (action_id == "search_in_folder") {
    focus_search_with_filter(layout_state, std::string{}, state->relative_path);
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    return true;
  }

  if (action_id == "symbol_info") {
    request_symbol_info_at(layout_state, state->editor_line, state->editor_col, state->anchor_x,
                           state->anchor_y);
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
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
    go_to_implementation(workspace, layout_state, symbols, state->editor_line, state->editor_col,
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

  if (action_id == "call_hierarchy") {
    focus_call_hierarchy(layout_state, state->editor_line, state->editor_col, state->symbol_name);
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    return true;
  }

  if (action_id == "find_references") {
    focus_search_with_filter(layout_state, state->symbol_name, std::string{});
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    return true;
  }

  if (action_id == "format_file") {
    const std::string path = state->absolute_path.empty()
                                 ? (workspace != nullptr ? workspace->active_file : std::string{})
                                 : state->absolute_path;
    if (path.empty()) {
      if (workspace != nullptr) {
        workspace->status_message = i18n::tr("status.no_file_to_format");
      }
      return true;
    }
    format_file_at_path(workspace, layout_state, symbols, path);
    if (focus != nullptr &&
        (state->kind == ContextMenuKind::EditorBackground ||
         state->kind == ContextMenuKind::EditorSymbol)) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  if (action_id == "apply_fix") {
    apply_problem_quick_fix(state, workspace, model, layout_state, symbols, symbol_indexer);
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  if (action_id == "add_to_watch") {
    if (model == nullptr || state->symbol_name.empty() || !on_command) {
      return true;
    }
    model->add_watch(state->symbol_name);
    UiCommand command;
    command.kind = UiCommandKind::kAddWatch;
    command.expression = state->symbol_name;
    assign_watch_frame(&command, model);
    on_command(command);
    if (layout_state != nullptr) {
      layout_state->right_panel_active_section = 1;
    }
    return true;
  }

  if (action_id == "hardware_watch") {
    if (model == nullptr || state->symbol_name.empty() || !on_command) {
      return true;
    }
    model->add_hardware_watch(state->symbol_name, state->symbol_name);
    UiCommand command;
    command.kind = UiCommandKind::kAddHardwareWatch;
    command.expression = state->symbol_name;
    command.hardware_watch_index = static_cast<int>(model->hardware_watches.size()) - 1;
    on_command(command);
    if (layout_state != nullptr) {
      layout_state->right_panel_active_section = 1;
    }
    return true;
  }

  return false;
}

bool commit_rename(ContextMenuState* state, WorkspaceModel* workspace, DebugModel* model,
                   FocusManagerState* focus, MainLayoutState* layout_state,
                   const std::shared_ptr<ISymbolProvider>& symbols, WorkspaceIndexer* indexer,
                   SymbolWorkspaceIndexer* symbol_indexer) {
  if (state == nullptr) {
    return false;
  }
  const std::string new_name = trim_copy(state->rename_input);
  if (!is_valid_filename(new_name)) {
    if (workspace != nullptr) {
      workspace->status_message = i18n::tr("status.invalid_name");
    }
    return false;
  }

  if (state->kind == ContextMenuKind::EditorSymbol) {
    const bool ok = rename_symbol_with_lsp(state, workspace, layout_state, symbols, model,
                                           symbol_indexer, new_name);
    if (ok && focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return ok;
  }

  if (state->name_prompt_kind == NamePromptKind::CreateFile ||
      state->name_prompt_kind == NamePromptKind::CreateFolder) {
    return create_path(workspace, model, indexer, symbol_indexer, state->absolute_path,
                       state->relative_path, new_name,
                       state->name_prompt_kind == NamePromptKind::CreateFolder);
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

void focus_search_with_filter(MainLayoutState* layout_state, const std::string& query,
                              const std::string& path_filter) {
  if (layout_state == nullptr) {
    return;
  }
  layout_state->console_visible = true;
  layout_state->console_tabs.selected_tab = ConsolePanelTabs::kSearch;
  layout_state->right_panel_active_section = 0;
  if (!query.empty() || !path_filter.empty()) {
    layout_state->right_sidebar.pending_search_setup = true;
    layout_state->right_sidebar.pending_search_query = query;
    layout_state->right_sidebar.pending_search_path_filter = path_filter;
  }
  layout_state->right_sidebar.pending_focus_search = true;
  layout_state->text_input_focus = TextInputFocus::SearchQuery;
  layout_state->focus_sync_needed = true;
  UI_WAKE(layout_state, "wake");
}

bool context_menu_active(const ContextMenuState* state) {
  return state != nullptr && (state->open || state->rename_open || state->delete_confirm_open ||
                              state->indexer_paths_open || state->move_browser_open);
}

void context_menu_close(ContextMenuState* state, MainLayoutState* layout_state) {
  if (state == nullptr) {
    return;
  }
  state->open = false;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->indexer_paths_open = false;
  state->move_browser_open = false;
  state->indexer_paths_scroll = 0;
  state->indexer_paths_lines.clear();
  state->rename_skip_return = false;
  state->rename_input.clear();
  state->name_prompt_kind = NamePromptKind::Rename;
  state->selected = 0;
  state->row_boxes.clear();
  if (layout_state != nullptr) {
    layout_state->clickable.clear_hover_if(press_id::is_context_menu_hover);
    UI_WAKE(layout_state, "wake");
  }
}

void context_menu_open_file(ContextMenuState* state, int x, int y,
                            const std::string& absolute_path, const std::string& relative_path,
                            bool show_format, bool show_secondary_open, bool show_analyze_symbols) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->indexer_paths_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->relative_path = relative_path;
  if (show_analyze_symbols) {
    if (show_format) {
      if (show_secondary_open) {
        set_items(state, ContextMenuKind::File,
                  {{i18n::tr("context_menu.analyze_symbols"), "analyze_symbols"},
                   {i18n::tr("context_menu.open_secondary"), "open_file_secondary"},
                   {i18n::tr("context_menu.open_file"), "open_file"},
                   {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                   {i18n::tr("context_menu.format_file"), "format_file"},
                   {i18n::tr("context_menu.rename_file"), "rename_file"},
                   {i18n::tr("context_menu.move_to"), "move_to"},
                   {i18n::tr("context_menu.delete_file"), "delete_file"}});
      } else {
        set_items(state, ContextMenuKind::File,
                  {{i18n::tr("context_menu.analyze_symbols"), "analyze_symbols"},
                   {i18n::tr("context_menu.open_file"), "open_file"},
                   {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                   {i18n::tr("context_menu.format_file"), "format_file"},
                   {i18n::tr("context_menu.rename_file"), "rename_file"},
                   {i18n::tr("context_menu.move_to"), "move_to"},
                   {i18n::tr("context_menu.delete_file"), "delete_file"}});
      }
    } else if (show_secondary_open) {
      set_items(state, ContextMenuKind::File,
                {{i18n::tr("context_menu.analyze_symbols"), "analyze_symbols"},
                 {i18n::tr("context_menu.open_secondary"), "open_file_secondary"},
                 {i18n::tr("context_menu.open_file"), "open_file"},
                 {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                 {i18n::tr("context_menu.rename_file"), "rename_file"},
                 {i18n::tr("context_menu.move_to"), "move_to"},
                 {i18n::tr("context_menu.delete_file"), "delete_file"}});
    } else {
      set_items(state, ContextMenuKind::File,
                {{i18n::tr("context_menu.analyze_symbols"), "analyze_symbols"},
                 {i18n::tr("context_menu.open_file"), "open_file"},
                 {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                 {i18n::tr("context_menu.rename_file"), "rename_file"},
                 {i18n::tr("context_menu.move_to"), "move_to"},
                 {i18n::tr("context_menu.delete_file"), "delete_file"}});
    }
    return;
  }
  if (show_format) {
    if (show_secondary_open) {
      set_items(state, ContextMenuKind::File,
                {{i18n::tr("context_menu.open_secondary"), "open_file_secondary"},
                 {i18n::tr("context_menu.open_file"), "open_file"},
                 {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                 {i18n::tr("context_menu.format_file"), "format_file"},
                 {i18n::tr("context_menu.rename_file"), "rename_file"},
                 {i18n::tr("context_menu.move_to"), "move_to"},
                 {i18n::tr("context_menu.delete_file"), "delete_file"}});
    } else {
      set_items(state, ContextMenuKind::File,
                {{i18n::tr("context_menu.open_file"), "open_file"},
                 {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
                 {i18n::tr("context_menu.format_file"), "format_file"},
                 {i18n::tr("context_menu.rename_file"), "rename_file"},
                 {i18n::tr("context_menu.move_to"), "move_to"},
                 {i18n::tr("context_menu.delete_file"), "delete_file"}});
    }
    return;
  }
  if (show_secondary_open) {
    set_items(state, ContextMenuKind::File,
              {{i18n::tr("context_menu.open_secondary"), "open_file_secondary"},
               {i18n::tr("context_menu.open_file"), "open_file"},
               {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
               {i18n::tr("context_menu.rename_file"), "rename_file"},
               {i18n::tr("context_menu.move_to"), "move_to"},
               {i18n::tr("context_menu.delete_file"), "delete_file"}});
    return;
  }
  set_items(state, ContextMenuKind::File,
            {{i18n::tr("context_menu.open_file"), "open_file"},
             {i18n::tr("context_menu.indexer_paths"), "show_indexer_paths"},
             {i18n::tr("context_menu.rename_file"), "rename_file"},
             {i18n::tr("context_menu.move_to"), "move_to"},
             {i18n::tr("context_menu.delete_file"), "delete_file"}});
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
            {{i18n::tr("context_menu.create_folder"), "create_folder"},
             {i18n::tr("context_menu.create_file"), "create_file"},
             {i18n::tr("context_menu.move_to"), "move_to"},
             {i18n::tr("context_menu.rename_folder"), "rename_folder"},
             {i18n::tr("context_menu.search_in_folder"), "search_in_folder"},
             {i18n::tr("context_menu.delete_folder"), "delete_folder"}});
}

void context_menu_open_editor_symbol(ContextMenuState* state, int x, int y, int line, int col,
                                     int sym_start, int sym_end, const std::string& symbol,
                                     const std::string& absolute_path, bool show_call_hierarchy,
                                     const DebugModel* model) {
  if (state == nullptr || symbol.empty()) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->editor_line = line;
  state->editor_col = col;
  state->editor_sym_start = sym_start;
  state->editor_sym_end = sym_end;
  state->symbol_name = symbol;
  const bool show_format = is_lsp_trackable_path(absolute_path);
  const bool show_debug = debug_watches_available(model);
  const bool show_hw = hardware_watch_available(model);
  if (show_call_hierarchy) {
    if (show_format) {
      set_items(state, ContextMenuKind::EditorSymbol,
                {{i18n::tr("context_menu.go_definition"), "go_definition"},
                 {i18n::tr("context_menu.symbol_info"), "symbol_info"},
                 {i18n::tr("context_menu.go_implementation"), "go_implementation"},
                 {i18n::tr("context_menu.call_hierarchy"), "call_hierarchy"},
                 {i18n::tr("context_menu.rename_symbol"), "rename_symbol"},
                 {i18n::tr("context_menu.find_references"), "find_references"},
                 {i18n::tr("context_menu.format_file"), "format_file"}});
    } else {
      set_items(state, ContextMenuKind::EditorSymbol,
                {{i18n::tr("context_menu.go_definition"), "go_definition"},
                 {i18n::tr("context_menu.symbol_info"), "symbol_info"},
                 {i18n::tr("context_menu.go_implementation"), "go_implementation"},
                 {i18n::tr("context_menu.call_hierarchy"), "call_hierarchy"},
                 {i18n::tr("context_menu.rename_symbol"), "rename_symbol"},
                 {i18n::tr("context_menu.find_references"), "find_references"}});
    }
    if (show_debug) {
      append_debug_watch_items(state, show_hw);
    }
    return;
  }
  if (show_format) {
    set_items(state, ContextMenuKind::EditorSymbol,
              {{i18n::tr("context_menu.go_definition"), "go_definition"},
               {i18n::tr("context_menu.symbol_info"), "symbol_info"},
               {i18n::tr("context_menu.go_implementation"), "go_implementation"},
               {i18n::tr("context_menu.rename_symbol"), "rename_symbol"},
               {i18n::tr("context_menu.find_references"), "find_references"},
               {i18n::tr("context_menu.format_file"), "format_file"}});
  } else {
    set_items(state, ContextMenuKind::EditorSymbol,
              {{i18n::tr("context_menu.go_definition"), "go_definition"},
               {i18n::tr("context_menu.symbol_info"), "symbol_info"},
               {i18n::tr("context_menu.go_implementation"), "go_implementation"},
               {i18n::tr("context_menu.rename_symbol"), "rename_symbol"},
               {i18n::tr("context_menu.find_references"), "find_references"}});
  }
  if (show_debug) {
    append_debug_watch_items(state, show_hw);
  }
}

void context_menu_open_debug_symbol(ContextMenuState* state, int x, int y, int line, int col,
                                    const std::string& symbol, const std::string& absolute_path,
                                    const DebugModel* model) {
  if (state == nullptr || symbol.empty()) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->editor_line = line;
  state->editor_col = col;
  state->editor_sym_start = 0;
  state->editor_sym_end = 0;
  state->symbol_name = symbol;
  set_items(state, ContextMenuKind::DebugSymbol, {});
  append_debug_watch_items(state, hardware_watch_available(model));
}

void context_menu_open_editor_background(ContextMenuState* state, int x, int y,
                                         const std::string& absolute_path, int line, int col,
                                         bool show_call_hierarchy) {
  if (state == nullptr || absolute_path.empty()) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->absolute_path = absolute_path;
  state->relative_path.clear();
  state->editor_line = line;
  state->editor_col = col;
  state->symbol_name.clear();
  if (show_call_hierarchy) {
    set_items(state, ContextMenuKind::EditorBackground,
              {{i18n::tr("context_menu.call_hierarchy"), "call_hierarchy"},
               {i18n::tr("context_menu.format_file"), "format_file"}});
    return;
  }
  set_items(state, ContextMenuKind::EditorBackground,
            {{i18n::tr("context_menu.format_file"), "format_file"}});
}

void context_menu_open_problem(ContextMenuState* state, int x, int y, const std::string& path,
                               int line, int start_col, int end_col, const std::string& message,
                               bool lsp_available) {
  if (state == nullptr || path.empty() || !lsp_available) {
    return;
  }
  state->open = true;
  state->rename_open = false;
  state->delete_confirm_open = false;
  state->rename_skip_return = false;
  state->anchor_x = x;
  state->anchor_y = y;
  state->problem_path = path;
  state->problem_line = line;
  state->problem_start_col = start_col;
  state->problem_end_col = end_col;
  state->problem_message = message;
  set_items(state, ContextMenuKind::Problem, {{i18n::tr("context_menu.apply_fix"), "apply_fix"}});
}

Element render_indexer_paths_modal(ContextMenuState* state) {
  if (state == nullptr || !state->indexer_paths_open) {
    return text("");
  }
  constexpr int kVisibleLines = 18;
  const int total = static_cast<int>(state->indexer_paths_lines.size());
  const int max_scroll = std::max(0, total - kVisibleLines);
  state->indexer_paths_scroll = std::max(0, std::min(state->indexer_paths_scroll, max_scroll));

  Elements rows;
  const int start = state->indexer_paths_scroll;
  const int end = std::min(total, start + kVisibleLines);
  if (total == 0) {
    rows.push_back(text(i18n::tr("common.no_data")) | color(theme::Muted()));
  } else {
    for (int i = start; i < end; ++i) {
      const std::string& line = state->indexer_paths_lines[static_cast<std::size_t>(i)];
      const bool section = !line.empty() && line.rfind("  ", 0) != 0 && line.find(':') == std::string::npos;
      rows.push_back(text(" " + line + " ") |
                     color(section ? theme::Accent() : theme::Header()) | bold);
    }
  }

  Element dialog = ModalWindow(
      text(i18n::tr("context_menu.indexer_paths.title")) | color(theme::Accent()),
      vbox({
          vbox(std::move(rows)) | bgcolor(theme::PanelBg()),
          separator() | color(theme::AccentDim()),
          text(i18n::tr("context_menu.indexer_paths.footer")) | color(theme::Muted()),
      }));
  return CenteredModal(std::move(dialog));
}

Element render_delete_confirm_modal(const ContextMenuState* state) {
  if (state == nullptr || !state->delete_confirm_open) {
    return text("");
  }
  const std::string name = fs::path(state->absolute_path).filename().string();
  const bool is_folder = state->kind == ContextMenuKind::Folder;
  Element dialog = ModalWindow(
      text(i18n::tr(is_folder ? "context_menu.delete.folder_title"
                              : "context_menu.delete.title")) |
          color(theme::Accent()),
      vbox({
          text(i18n::tr_fmt(is_folder ? "context_menu.delete.folder_prompt"
                                      : "context_menu.delete.prompt",
                            {name})) |
              color(theme::Header()),
          is_folder ? text(i18n::tr("context_menu.delete.folder_warning")) |
                          color(theme::Warning())
                    : text(""),
          text(i18n::tr("common.footer.confirm_esc")) | color(theme::Muted()),
      }));
  return CenteredModal(std::move(dialog));
}

Element render_rename_modal(ContextMenuState* state) {
  if (state == nullptr || !state->rename_open) {
    return text("");
  }
  std::string line = state->rename_input;
  line.push_back('_');
  const std::string title =
      state->name_prompt_kind == NamePromptKind::CreateFolder
          ? i18n::tr("context_menu.create.folder")
          : state->name_prompt_kind == NamePromptKind::CreateFile
                ? i18n::tr("context_menu.create.file")
          : state->kind == ContextMenuKind::EditorSymbol
                ? i18n::tr("context_menu.rename.symbol")
          : state->kind == ContextMenuKind::Folder ? i18n::tr("context_menu.rename.folder")
                                                   : i18n::tr("context_menu.rename.file");
  Element dialog = ModalWindow(
      text(title) | color(theme::Accent()),
      vbox({ModalInputLine(line),
            text(i18n::tr("common.footer.confirm_esc")) | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

void activate_move_browser_row(ContextMenuState* state, MainLayoutState* layout_state, int row) {
  if (state == nullptr || row < 0 || row >= static_cast<int>(state->move_browser.entries.size())) {
    return;
  }
  trigger_press(layout_state, press_id::f3_browser_row(row));
  state->move_browser.selected = row;
  const auto& entry = state->move_browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->move_browser.browser_path = entry.path;
    state->move_browser.reload_browser_entries(true);
  }
}

bool update_move_browser_hover(ContextMenuState* state, MainLayoutState* layout_state, int x,
                               int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto local = local_row_in_box(state->move_browser.browser_list_box, x, y);
  if (local.has_value()) {
    const int row = state->move_browser.browser_list_start + *local;
    if (row >= 0 && row < static_cast<int>(state->move_browser.entries.size())) {
      layout_state->clickable.set_hover(press_id::f3_browser_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_f3_hover);
    }
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f3_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

Element render_move_browser_modal(ContextMenuState* state, MainLayoutState* layout_state) {
  if (state == nullptr || !state->move_browser_open) {
    return text("");
  }
  state->move_browser.ensure_browser_entries();

  Elements body;
  body.push_back(text(state->move_browser.browser_path) | color(theme::Muted()));
  body.push_back(separator());

  const int max_rows = 14;
  state->move_browser.browser_list_start = std::max(
      0, std::min(state->move_browser.selected,
                  std::max(0, static_cast<int>(state->move_browser.entries.size()) - max_rows)));
  const int start = state->move_browser.browser_list_start;
  const int end = std::min(static_cast<int>(state->move_browser.entries.size()), start + max_rows);
  Elements list_rows;
  for (int i = start; i < end; ++i) {
    const auto& row = state->move_browser.entries[static_cast<std::size_t>(i)];
    std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                          : i18n::tr("common.browser.file_indent");
    const std::string row_id = press_id::f3_browser_row(i);
    const bool selected = i == state->move_browser.selected;
    const bool hovered = layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
    const bool pressed = layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
    Element line = text(prefix + row.name);
    if (row.is_directory) {
      line = line | color(theme::Accent());
    }
    line = StyleListRow(std::move(line), selected, hovered, pressed);
    list_rows.push_back(line);
  }
  if (list_rows.empty()) {
    list_rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
  }
  body.push_back(vbox(std::move(list_rows)) | reflect(state->move_browser.browser_list_box));

  Element dialog = ModalWindow(
      text(i18n::tr("context_menu.move_browser.title")) | color(theme::Accent()),
      vbox({
          vbox(std::move(body)) | flex | bgcolor(theme::PanelBg()),
          separator(),
          text(i18n::tr("context_menu.move_browser.footer")) | color(theme::Muted()),
      }));
  return CenteredModal(std::move(dialog));
}

Element render_context_menu_overlay(ContextMenuState* state, MainLayoutState* layout_state,
                                    Element base) {
  if (state == nullptr) {
    return text("");
  }

  if (state->indexer_paths_open) {
    return ScreenModalOverlay(std::move(base), render_indexer_paths_modal(state));
  }

  if (state->move_browser_open) {
    return ScreenModalOverlay(std::move(base), render_move_browser_modal(state, layout_state));
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
  if (!hover_effects_enabled()) {
    return false;
  }
  if (state == nullptr || layout_state == nullptr || !state->open || state->rename_open ||
      state->delete_confirm_open || state->indexer_paths_open || state->move_browser_open ||
      m.motion != Mouse::Moved) {
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
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

bool handle_context_menu_mouse(ContextMenuState* state, MainLayoutState* layout_state,
                               const Mouse& m, int* clicked_row) {
  if (clicked_row != nullptr) {
    *clicked_row = -1;
  }
  if (state == nullptr || !state->open || state->rename_open || state->delete_confirm_open ||
      state->indexer_paths_open || state->move_browser_open) {
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
                              WorkspaceModel* secondary_workspace, DebugModel* model,
                              FocusManagerState* focus, MainLayoutState* layout_state,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              WorkspaceIndexer* indexer, SymbolWorkspaceIndexer* symbol_indexer,
                              const WorkspaceConfig* workspace_config, int editor_visible_lines,
                              CommandCallback on_command, const Event& event) {
  if (state == nullptr || !context_menu_active(state)) {
    return false;
  }

  if (state->indexer_paths_open) {
    if (event == Event::Escape) {
      context_menu_close(state, layout_state);
      return true;
    }
    constexpr int kVisibleLines = 18;
    const int total = static_cast<int>(state->indexer_paths_lines.size());
    const int max_scroll = std::max(0, total - kVisibleLines);
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      state->indexer_paths_scroll = std::min(state->indexer_paths_scroll + 1, max_scroll);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->indexer_paths_scroll = std::max(0, state->indexer_paths_scroll - 1);
      return true;
    }
    if (event == Event::PageDown) {
      state->indexer_paths_scroll = std::min(state->indexer_paths_scroll + kVisibleLines, max_scroll);
      return true;
    }
    if (event == Event::PageUp) {
      state->indexer_paths_scroll = std::max(0, state->indexer_paths_scroll - kVisibleLines);
      return true;
    }
    return true;
  }

  if (state->move_browser_open) {
    state->move_browser.ensure_browser_entries();
    if (event == Event::Escape) {
      context_menu_close(state, layout_state);
      return true;
    }
    if (event == Event::Character('a') || event == Event::Character('A')) {
      if (is_directory_path(state->move_browser.browser_path)) {
        if (move_path(workspace, model, indexer, symbol_indexer, state->absolute_path,
                      state->relative_path, state->move_browser.browser_path,
                      state->move_is_dir)) {
          context_menu_close(state, layout_state);
          if (layout_state != nullptr) {
            UI_WAKE(layout_state, "wake");
          }
        }
      }
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      state->move_browser.selected = std::min(
          state->move_browser.selected + 1,
          std::max(0, static_cast<int>(state->move_browser.entries.size()) - 1));
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->move_browser.selected = std::max(0, state->move_browser.selected - 1);
      return true;
    }
    if (event == Event::PageDown) {
      state->move_browser.selected = std::min(
          state->move_browser.selected + 12,
          std::max(0, static_cast<int>(state->move_browser.entries.size()) - 1));
      return true;
    }
    if (event == Event::PageUp) {
      state->move_browser.selected = std::max(0, state->move_browser.selected - 12);
      return true;
    }
    if (event == Event::Return) {
      activate_move_browser_row(state, layout_state, state->move_browser.selected);
      return true;
    }
    return true;
  }

  if (state->delete_confirm_open) {
    if (event == Event::Escape) {
      context_menu_close(state, layout_state);
      return true;
    }
    if (event == Event::Return) {
      const bool is_dir = state->kind == ContextMenuKind::Folder;
      delete_path(workspace, model, indexer, symbol_indexer, state->absolute_path,
                  state->relative_path, is_dir);
      context_menu_close(state, layout_state);
      if (layout_state != nullptr) {
        layout_state->panel_render_cache.mark_dirty(UiPanelId::FileTree);
        UI_WAKE(layout_state, "wake");
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
      if (commit_rename(state, workspace, model, focus, layout_state, symbols, indexer,
                        symbol_indexer)) {
        context_menu_close(state, layout_state);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
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
                     secondary_workspace, model, focus, layout_state, symbols, indexer,
                     symbol_indexer, workspace_config, editor_visible_lines, on_command);
      if (!state->rename_open && !state->delete_confirm_open && !state->indexer_paths_open &&
          !state->move_browser_open) {
        context_menu_close(state, layout_state);
      }
      if (layout_state != nullptr) {
        UI_WAKE(layout_state, "wake");
      }
    }
    return true;
  }
  return true;
}

Component MakeContextMenuOverlay(Component main, ContextMenuState* state, WorkspaceModel* workspace,
                                 WorkspaceModel* secondary_workspace, DebugModel* model,
                                 FocusManagerState* focus, MainLayoutState* layout_state,
                                 const std::shared_ptr<ISymbolProvider>& symbols,
                                 WorkspaceIndexer* indexer,
                                 SymbolWorkspaceIndexer* symbol_indexer,
                                 const WorkspaceConfig* workspace_config,
                                 std::function<int()> editor_visible_lines,
                                 CommandCallback on_command) {
  auto run_selected_action = [=](int row) {
    if (state == nullptr || row < 0 || row >= static_cast<int>(state->action_ids.size())) {
      return;
    }
    const int visible = editor_visible_lines ? editor_visible_lines() : 24;
    execute_action(state, state->action_ids[static_cast<std::size_t>(row)], workspace,
                   secondary_workspace, model, focus, layout_state, symbols, indexer,
                   symbol_indexer, workspace_config, visible, on_command);
    if (!state->rename_open && !state->delete_confirm_open && !state->indexer_paths_open &&
        !state->move_browser_open) {
      context_menu_close(state, layout_state);
    }
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  };

  auto handler = [=](Event event) {
    if (state == nullptr || !context_menu_active(state)) {
      return false;
    }
    if (event.is_mouse()) {
      if (state->move_browser_open) {
        const auto& m = event.mouse();
        if (m.motion == Mouse::Moved) {
          update_move_browser_hover(state, layout_state, m.x, m.y);
          return true;
        }
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
          if (state->move_browser.browser_list_box.Contain(m.x, m.y)) {
            const int row = state->move_browser.browser_list_start +
                            (m.y - state->move_browser.browser_list_box.y_min);
            activate_move_browser_row(state, layout_state, row);
          }
          return true;
        }
        return true;
      }
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
    return handle_context_menu_keys(state, workspace, secondary_workspace, model, focus,
                                    layout_state, symbols, indexer, symbol_indexer,
                                    workspace_config, visible, on_command, event);
  };

  return Renderer(CatchEvent(main, handler),
                  [main, state, layout_state] {
                    Element base = main->Render();
                    if (state == nullptr || !context_menu_active(state)) {
                      return base;
                    }
                    Element overlay = render_context_menu_overlay(state, layout_state, base);
                    if (state->rename_open || state->delete_confirm_open ||
                        state->indexer_paths_open || state->move_browser_open) {
                      return overlay;
                    }
                    return dbox({std::move(base), std::move(overlay)});
                  });
}

}  // namespace tgdb
