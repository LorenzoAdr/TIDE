#include "ui/editor_panel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "editor/clipboard.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/editor_render.hpp"
#include "editor/editor_state.hpp"
#include "editor/text_ops.hpp"
#include "editor/text_search.hpp"
#include "ftxui/component/component.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "symbols/symbol_utils.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string buffer_text(const EditorBuffer& buffer) {
  std::string text;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    if (i > 0) {
      text.push_back('\n');
    }
    text += buffer.lines[i];
  }
  return text;
}

struct EditorPanelState {
  Box code_box;
  Box gutter_box;
  uint64_t last_view_token = 0;
  std::string last_path;
};

struct GotoLineState {
  bool open = false;
  std::string query;
};

struct CompletionState {
  bool open = false;
  bool workspace_index = false;
  bool index_scanning = false;
  std::string prefix;
  std::string query;
  std::string sync_key;
  std::vector<IndexedSymbol> all_symbols;
  std::vector<IndexedSymbol> matches;
  int selected = 0;
  int replace_line = 0;
  int replace_start = 0;
  int replace_end = 0;

  static std::string to_lower(std::string value) {
    for (char& c : value) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
  }

  static bool prefix_match(const std::string& name, const std::string& query) {
    if (query.empty()) {
      return true;
    }
    const std::string n = to_lower(name);
    const std::string q = to_lower(query);
    return n.size() >= q.size() && n.compare(0, q.size(), q) == 0;
  }

  void refresh_matches() {
    matches.clear();
    constexpr int kMaxMatches = 200;
    for (const auto& sym : all_symbols) {
      if (prefix_match(symbol_insert_name(sym.display_name), query)) {
        matches.push_back(sym);
        if (static_cast<int>(matches.size()) >= kMaxMatches) {
          break;
        }
      }
    }
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
  }

  void sync_symbols(WorkspaceModel* workspace,
                    const std::shared_ptr<ISymbolProvider>& symbols,
                    SymbolWorkspaceIndexer* symbol_indexer) {
    const std::string workspace_root = workspace->root;
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;

    index_scanning = symbol_indexer != nullptr && symbol_indexer->scanning();
    workspace_index = false;

    if (symbols && !symbols->indexes_workspace_bulk() && !workspace_root.empty()) {
      const std::string key = workspace_root + "|lsp|" + query + "|" + path;
      if (sync_key != key) {
        sync_key = key;
        workspace_index = true;
        index_scanning = false;
        all_symbols.clear();
        if (query.empty() && !path.empty()) {
          for (const auto& sym : symbols->symbols_for_file(path)) {
            IndexedSymbol entry;
            entry.display_name = sym.name;
            entry.kind = sym.kind;
            entry.line = sym.line;
            entry.file = sym.file;
            all_symbols.push_back(std::move(entry));
          }
        } else {
          for (const auto& sym : symbols->workspace_symbols(workspace_root, query)) {
            IndexedSymbol entry;
            entry.display_name = sym.name;
            entry.kind = sym.kind;
            entry.line = sym.line;
            entry.file = sym.file;
            all_symbols.push_back(std::move(entry));
          }
        }
        refresh_matches();
      }
      return;
    }

    if (symbol_indexer != nullptr && !workspace_root.empty()) {
      const auto snap = symbol_indexer->snapshot();
      if (snap && snap->workspace_root == workspace_root) {
        index_scanning = symbol_indexer->scanning();
        if (!snap->symbols.empty() || !index_scanning) {
          const std::string key = workspace_root + "|workspace";
          if (sync_key != key) {
            sync_key = key;
            all_symbols = snap->symbols;
            workspace_index = true;
            refresh_matches();
          }
          return;
        }
      }
    }

    const std::string key = workspace_root + "|" + path;
    if (sync_key == key) {
      return;
    }
    sync_key = key;
    workspace_index = false;
    all_symbols.clear();
    if (symbols && !path.empty()) {
      for (const auto& sym : symbols->symbols_for_file(path)) {
        IndexedSymbol entry;
        entry.display_name = sym.name;
        entry.kind = sym.kind;
        entry.line = sym.line;
        all_symbols.push_back(std::move(entry));
      }
    }
    refresh_matches();
  }

  void close(MainLayoutState* layout_state) {
    open = false;
    prefix.clear();
    query.clear();
    selected = 0;
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
  }
};

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll(int total, int visible) { return std::max(0, total - visible); }

int line_number_width(int total_lines) {
  const int digits = std::max(1, static_cast<int>(std::to_string(total_lines).size()));
  return digits + 1;
}

std::string format_line_number(int line_no, int width) {
  std::string text = std::to_string(line_no);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

Element vertical_scrollbar(int total_lines, int scroll, int visible_lines, int bar_height) {
  Elements track;
  if (bar_height <= 0) {
    return text("");
  }

  if (total_lines <= visible_lines) {
    for (int i = 0; i < bar_height; ++i) {
      track.push_back(text("│") | color(theme::Muted()));
    }
    return vbox(std::move(track));
  }

  const int thumb_height = std::max(1, visible_lines * bar_height / total_lines);
  const int max_scroll_pos = total_lines - visible_lines;
  const int thumb_y = max_scroll_pos > 0
                          ? (scroll * (bar_height - thumb_height)) / max_scroll_pos
                          : 0;

  for (int i = 0; i < bar_height; ++i) {
    if (i >= thumb_y && i < thumb_y + thumb_height) {
      track.push_back(text("┃") | color(theme::Accent()));
    } else {
      track.push_back(text("│") | color(theme::Muted()));
    }
  }
  return vbox(std::move(track));
}

bool find_input_active(MainLayoutState* layout_state, const EditorFindState& find) {
  return find.open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorFind;
}

bool goto_input_active(MainLayoutState* layout_state, bool goto_open) {
  return goto_open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorGotoLine;
}

bool completion_input_active(MainLayoutState* layout_state, bool completion_open) {
  return completion_open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorCompletion;
}

void activate_find(EditorFindState* find, EditorBuffer* buffer, MainLayoutState* layout_state,
                   Component find_input) {
  if (!find->open) {
    open_find_bar(find, buffer);
  } else {
    find->refresh_matches(*buffer);
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::EditorFind;
  }
  find_input->TakeFocus();
}

bool handle_editor_escape(EditorBuffer* buffer, EditorFindState* find,
                          MainLayoutState* layout_state, bool* goto_open,
                          CompletionState* completion) {
  if (completion != nullptr && completion->open) {
    completion->close(layout_state);
    return true;
  }
  if (find != nullptr && find->open) {
    close_find_bar(find);
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (goto_open != nullptr && *goto_open) {
    *goto_open = false;
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (buffer->multi_cursor_active()) {
    exit_multi_cursor_mode(buffer);
    return true;
  }
  if (buffer->primary().has_selection()) {
    clear_primary_selection(buffer);
    return true;
  }
  return true;
}

bool handle_editor_mouse(EditorBuffer* buffer, FocusManagerState* focus,
                         EditorFindState* find, MainLayoutState* layout_state,
                         const EditorPanelState& panel, Event event, int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }

  const auto& m = event.mouse();
  const bool in_code = panel.code_box.Contain(m.x, m.y);
  const bool in_gutter = panel.gutter_box.Contain(m.x, m.y);
  if (!in_code && !in_gutter) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    buffer->scroll = std::max(0, buffer->scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    buffer->scroll = std::min(
        max_scroll(static_cast<int>(buffer->lines.size()), visible_lines),
        buffer->scroll + 3);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    focus->region = FocusRegion::Editor;
    if (find != nullptr && find->open) {
      close_find_bar(find);
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    }
    const int row = m.y - (in_gutter ? panel.gutter_box.y_min : panel.code_box.y_min);
    const int line =
        std::max(0, std::min(buffer->scroll + row,
                             static_cast<int>(buffer->lines.size()) - 1));
    int col = buffer->primary_col();
    if (in_code) {
      col = std::max(0, m.x - panel.code_box.x_min);
      const int line_len = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
      col = std::min(col, line_len);
    }
    buffer->reset_to_single_cursor(line, col);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }

  return false;
}

bool handle_goto_line_keys(GotoLineState* goto_state, MainLayoutState* layout_state,
                           EditorBuffer* buffer, Event event, int visible_lines) {
  if (goto_state == nullptr || !goto_state->open) {
    return false;
  }

  if (event == Event::Escape) {
    goto_state->open = false;
    goto_state->query.clear();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (event == Event::Return) {
    if (!goto_state->query.empty()) {
      char* end = nullptr;
      const long parsed = std::strtol(goto_state->query.c_str(), &end, 10);
      if (end != goto_state->query.c_str() && parsed > 0) {
        goto_buffer_line(buffer, static_cast<int>(parsed), visible_lines);
      }
    }
    goto_state->open = false;
    goto_state->query.clear();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (event == Event::Backspace) {
    if (!goto_state->query.empty()) {
      goto_state->query.pop_back();
    }
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && std::isdigit(static_cast<unsigned char>(ch[0]))) {
      goto_state->query += ch;
    }
    return true;
  }
  return true;
}

void open_completion(CompletionState* completion, WorkspaceModel* workspace,
                     const std::shared_ptr<ISymbolProvider>& symbols,
                     SymbolWorkspaceIndexer* symbol_indexer, EditorBuffer* buffer,
                     EditorFindState* find, MainLayoutState* layout_state) {
  if (completion == nullptr) {
    return;
  }
  if (find != nullptr && find->open) {
    close_find_bar(find);
  }
  completion->open = true;
  completion->prefix = word_at_cursor(*buffer, buffer->primary());
  completion->query = completion->prefix;
  completion->selected = 0;
  completion->replace_line = buffer->primary().head.line;
  ident_range_at_cursor(*buffer, buffer->primary(), &completion->replace_start,
                        &completion->replace_end);
  completion->sync_key.clear();
  completion->sync_symbols(workspace, symbols, symbol_indexer);
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::EditorCompletion;
  }
}

bool accept_completion(CompletionState* completion, EditorBuffer* buffer,
                       MainLayoutState* layout_state, int visible_lines) {
  if (completion == nullptr || completion->matches.empty()) {
    return false;
  }
  completion->selected =
      std::max(0, std::min(completion->selected, static_cast<int>(completion->matches.size()) - 1));
  const auto& sym = completion->matches[static_cast<std::size_t>(completion->selected)];
  replace_text_range(buffer, completion->replace_line, completion->replace_start,
                     completion->replace_end, symbol_insert_name(sym.display_name));
  ensure_scroll_visible(buffer, visible_lines);
  completion->close(layout_state);
  return true;
}

bool handle_completion_keys(CompletionState* completion, WorkspaceModel* workspace,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              SymbolWorkspaceIndexer* symbol_indexer,
                              MainLayoutState* layout_state, EditorBuffer* buffer,
                              Event event, int visible_lines) {
  if (completion == nullptr || !completion->open) {
    return false;
  }

  completion->sync_symbols(workspace, symbols, symbol_indexer);

  if (event == Event::Escape) {
    completion->close(layout_state);
    return true;
  }
  if (event == Event::Return || event == Event::Tab) {
    return accept_completion(completion, buffer, layout_state, visible_lines);
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    if (!completion->matches.empty()) {
      completion->selected = std::min(completion->selected + 1,
                                      static_cast<int>(completion->matches.size()) - 1);
    }
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    completion->selected = std::max(0, completion->selected - 1);
    return true;
  }
  if (event_is_completion(event)) {
    if (!completion->matches.empty()) {
      completion->selected =
          (completion->selected + 1) % static_cast<int>(completion->matches.size());
    }
    return true;
  }
  if (event == Event::Backspace) {
    if (!completion->query.empty()) {
      completion->query.pop_back();
      completion->selected = 0;
      completion->refresh_matches();
    }
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      completion->query += ch;
      completion->selected = 0;
      completion->refresh_matches();
    }
    return true;
  }
  return true;
}

bool handle_editor_keys(WorkspaceModel* workspace, FocusManagerState* focus,
                        EditorFindState* find, GotoLineState* goto_state,
                        CompletionState* completion,
                        const std::shared_ptr<ISymbolProvider>& symbols,
                        WorkspaceIndexer* file_indexer,
                        SymbolWorkspaceIndexer* symbol_indexer,
                        MainLayoutState* layout_state, Event event, int visible_lines) {
  if (focus->region != FocusRegion::Editor) {
    return false;
  }
  if (find != nullptr && find_input_active(layout_state, *find)) {
    return false;
  }

  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  buffer->ensure_cursors();

  if (completion != nullptr && completion->open) {
    return handle_completion_keys(completion, workspace, symbols, symbol_indexer, layout_state,
                                  buffer, event, visible_lines);
  }

  if (goto_state != nullptr && goto_state->open) {
    return handle_goto_line_keys(goto_state, layout_state, buffer, event, visible_lines);
  }

  if (event == Event::Escape) {
    return handle_editor_escape(buffer, find, layout_state,
                                goto_state != nullptr ? &goto_state->open : nullptr, completion);
  }
  if (event_is_completion(event)) {
    open_completion(completion, workspace, symbols, symbol_indexer, buffer, find, layout_state);
    return true;
  }
  if (event_is_ctrl_g(event)) {
    if (goto_state != nullptr) {
      goto_state->open = true;
      goto_state->query.clear();
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::EditorGotoLine;
      }
    }
    return true;
  }
  if (event_is_ctrl_z(event)) {
    undo_edit(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_c(event)) {
    copy_selection(buffer);
    return true;
  }
  if (event_is_ctrl_v(event)) {
    paste_text(buffer, editor_clipboard());
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::CtrlD) {
    add_next_selection_match(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_shift_l(event)) {
    select_all_matches(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::CtrlS) {
    workspace->save_buffer();
    if (!workspace->root.empty() && !workspace->buffer.path.empty()) {
      std::error_code ec;
      const auto rel = std::filesystem::relative(
          std::filesystem::path(workspace->buffer.path),
          std::filesystem::path(workspace->root), ec);
      if (!ec) {
        const std::string rel_str = rel.generic_string();
        if (symbols) {
          symbols->on_document_changed(workspace->buffer.path, buffer_text(*buffer));
        }
        if (file_indexer != nullptr) {
          file_indexer->upsert_file(workspace->root, rel_str, workspace->buffer.path);
        }
        if (symbol_indexer != nullptr) {
          symbol_indexer->reindex_file(workspace->root, rel_str, workspace->buffer.path);
        }
      }
    }
    return true;
  }

  const bool extend = event_is_shift_left(event) || event_is_shift_right(event) ||
                      event_is_shift_up(event) || event_is_shift_down(event);

  if (event == Event::ArrowLeft || event_is_shift_left(event)) {
    move_primary_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowRight || event_is_shift_right(event)) {
    move_primary_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_left(event)) {
    move_primary_word_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_right(event)) {
    move_primary_word_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_shift_up(event)) {
    extend_block_selection_vertical(buffer, -1);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_shift_down(event)) {
    extend_block_selection_vertical(buffer, 1);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowUp || event_is_shift_up(event)) {
    move_primary_up(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowDown || event_is_shift_down(event)) {
    move_primary_down(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Home) {
    move_primary_home(buffer, false);
    return true;
  }
  if (event == Event::End) {
    move_primary_end(buffer, false);
    return true;
  }
  if (event == Event::Backspace) {
    backspace(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Delete) {
    delete_char(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Return) {
    newline(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Tab) {
    insert_char(buffer, '\t');
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::PageDown) {
    move_primary_page_down(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::PageUp) {
    move_primary_page_up(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      insert_char(buffer, ch[0]);
      ensure_scroll_visible(buffer, visible_lines);
      return true;
    }
  }
  return false;
}

std::string build_editor_title(const EditorBuffer& buffer, const EditorFindState& find) {
  std::string title = "Editor";
  if (!buffer.path.empty()) {
    title = std::filesystem::path(buffer.path).filename().string();
    if (buffer.dirty) {
      title += " *";
    }
  }
  title += "  L" + std::to_string(buffer.primary_line() + 1) + ":" +
           std::to_string(buffer.primary_col() + 1);
  if (buffer.multi_cursor_active()) {
    title += "  [" + std::to_string(buffer.cursors.size()) + " cursores]";
  }
  if (find.open) {
    title += "  [buscar]";
  }
  return title;
}

Element make_completion_overlay(const CompletionState& completion_state,
                                const EditorBuffer& buffer, int gutter_width,
                                int visible_lines) {
  if (!completion_state.open) {
    return text("");
  }

  const int max_rows = 8;
  const int start = std::max(
      0, std::min(completion_state.selected,
                  std::max(0, static_cast<int>(completion_state.matches.size()) - max_rows)));
  const int end =
      std::min(static_cast<int>(completion_state.matches.size()), start + max_rows);

  Elements rows;
  for (int i = start; i < end; ++i) {
    const auto& sym = completion_state.matches[static_cast<std::size_t>(i)];
    std::string label = symbol_insert_name(sym.display_name);
    if (completion_state.workspace_index && !sym.file.empty()) {
      label = sym.file + " · " + label;
    }
    Element row = text(" " + label) | color(theme::Header());
    if (i == completion_state.selected) {
      row = row | inverted | bgcolor(theme::EditorLineHi());
    } else {
      row = row | bgcolor(theme::CodeBg());
    }
    rows.push_back(row);
  }
  if (rows.empty()) {
    const char* msg = completion_state.index_scanning && completion_state.workspace_index
                          ? " indexando…"
                          : " —";
    rows.push_back(text(msg) | color(theme::Muted()) | bgcolor(theme::CodeBg()));
  }

  const int popup_rows = static_cast<int>(rows.size());
  Element popup = vbox(std::move(rows)) | bgcolor(theme::CodeBg());

  const int caret_row = std::max(0, buffer.primary_line() - buffer.scroll);
  const int caret_col = std::max(0, buffer.primary_col());
  const int x_pad = gutter_width + 1;

  const bool place_above = visible_lines > 0 && caret_row + popup_rows + 1 >= visible_lines;
  const int y_pad = place_above ? std::max(0, caret_row - popup_rows) : caret_row + 1;

  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, x_pad + caret_col),
                           popup | clear_under,
                           filler()}),
                     filler()}) |
                   flex});
}

Element make_goto_line_overlay(const GotoLineState& goto_state) {
  if (!goto_state.open) {
    return text("");
  }
  std::string input_line = goto_state.query;
  input_line.push_back('_');
  Element dialog = ModalWindow(
      text("Ir a línea") | color(theme::Accent()),
      vbox({ModalInputLine(input_line),
            text("Enter ir  Esc cancelar") | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

}  // namespace

Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                          MainLayoutState* layout_state,
                          std::shared_ptr<ISymbolProvider> symbols,
                          WorkspaceIndexer* file_indexer,
                          SymbolWorkspaceIndexer* symbol_indexer) {
  auto panel_state = std::make_shared<EditorPanelState>();
  auto find_state = std::make_shared<EditorFindState>();
  auto goto_state = std::make_shared<GotoLineState>();
  auto completion_state = std::make_shared<CompletionState>();

  InputOption find_input_opt = InputOption::Default();
  find_input_opt.multiline = false;
  find_input_opt.transform = [](InputState state) {
    state.element |= bgcolor(theme::CodeBg()) | color(theme::WatchInput());
    if (state.is_placeholder) {
      state.element |= dim;
    }
    if (state.focused) {
      state.element |= inverted;
    }
    return state.element;
  };
  auto find_input = Input(&find_state->query, "Buscar...", find_input_opt);

  auto find_row = Container::Horizontal({
      find_input | flex,
      Renderer([find_state] {
        return text(" (" + std::to_string(find_state->matches.size()) + ") Enter Esc ") |
               color(theme::Muted());
      }),
  });
  auto find_row_active = Maybe(find_row, [find_state] { return find_state->open; });

  auto modal_overlay = Renderer([find_row_active, find_state, goto_state, completion_state,
                                 workspace, symbols, symbol_indexer, panel_state] {
    if (completion_state->open) {
      workspace->ensure_buffer();
      const EditorBuffer& buffer = workspace->buffer;
      completion_state->sync_symbols(workspace, symbols, symbol_indexer);
      const int total = static_cast<int>(buffer.lines.size());
      const int gutter_w = line_number_width(total);
      const int visible = visible_line_count(panel_state->code_box);
      return make_completion_overlay(*completion_state, buffer, gutter_w, visible);
    }
    if (goto_state->open) {
      return make_goto_line_overlay(*goto_state);
    }
    if (!find_state->open) {
      return text("");
    }
    return dbox({text(""),
                 vbox({
                     hbox({filler(),
                           find_row_active->Render() | border | bgcolor(theme::PanelBg()) |
                               size(WIDTH, GREATER_THAN, 34) | size(HEIGHT, EQUAL, 3)}),
                     filler(),
                 }) |
                     flex});
  });

  auto code_view = Renderer([workspace, focus, panel_state, find_state, symbols] {
    workspace->ensure_buffer();
    EditorBuffer& buffer = workspace->buffer;
    buffer.ensure_cursors();

    if (buffer.path != panel_state->last_path) {
      panel_state->last_path = buffer.path;
      if (symbols && !buffer.path.empty()) {
        symbols->on_document_opened(buffer.path, buffer_text(buffer));
      }
      buffer.scroll = std::max(0, buffer.primary_line() - 2);
    }

    if (buffer.view_token != panel_state->last_view_token) {
      panel_state->last_view_token = buffer.view_token;
      if (find_state->open) {
        find_state->refresh_matches(buffer);
      }
    }

    if (find_state->open && !find_state->query.empty()) {
      find_state->refresh_matches(buffer);
    }

    const int visible = visible_line_count(panel_state->code_box);

    const int total = static_cast<int>(buffer.lines.size());
    const int start = buffer.scroll;
    const int end = std::min(total, start + visible);
    const int gutter_w = line_number_width(total);
    const bool editor_focused = focus->region == FocusRegion::Editor;
    const std::vector<TextMatch>* find_matches =
        find_state->open && !find_state->matches.empty() ? &find_state->matches : nullptr;

    Elements gutter_rows;
    Elements code_rows;
    for (int i = start; i < end; ++i) {
      const bool is_primary = (i == buffer.primary_line());
      const Decorator row_bg =
          is_primary ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());

      gutter_rows.push_back(text(format_line_number(i + 1, gutter_w)) | color(theme::Muted()) |
                            row_bg);

      code_rows.push_back(RenderEditorLine(buffer.lines[static_cast<std::size_t>(i)], i, buffer,
                                           editor_focused, find_matches));
    }
    if (code_rows.empty()) {
      gutter_rows.push_back(text(format_line_number(1, gutter_w)) | color(theme::Muted()) |
                            bgcolor(theme::CodeBg()));
      code_rows.push_back(text(" ") | bgcolor(theme::CodeBg()));
    }

    const int rendered_lines = static_cast<int>(code_rows.size());

    Element gutter = vbox(std::move(gutter_rows)) | reflect(panel_state->gutter_box) |
                     bgcolor(theme::CodeBg());
    Element code = vbox(std::move(code_rows)) | flex | reflect(panel_state->code_box) |
                   bgcolor(theme::CodeBg());
    Element scrollbar = vertical_scrollbar(total, buffer.scroll, visible, rendered_lines);

    return hbox({gutter, separator() | color(theme::AccentDim()), code | flex, scrollbar}) |
           frame | flex | bgcolor(theme::CodeBg());
  });

  // En Stacked, el primer hijo se dibuja encima (FTXUI invierte el dbox interno).
  auto editor_stack = Container::Stacked({
      modal_overlay,
      code_view | flex,
  });

  auto panel = Renderer(editor_stack, [workspace, find_state, editor_stack] {
    workspace->ensure_buffer();
    return MakePanel(build_editor_title(workspace->buffer, *find_state),
                     editor_stack->Render(), theme::CodeBg());
  });

  auto dispatch_editor_keys = [workspace, focus, panel_state, find_state, goto_state,
                               completion_state, symbols, file_indexer, symbol_indexer,
                               layout_state, find_input](Event event) {
    if (layout_state != nullptr &&
        layout_state->text_input_focus == TextInputFocus::Console) {
      return false;
    }
    if (focus->region != FocusRegion::Editor) {
      return false;
    }
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    const int visible = visible_line_count(panel_state->code_box);

    if (find_state->open && event == Event::Escape) {
      close_find_bar(find_state.get());
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }
    if (find_input_active(layout_state, *find_state) && event == Event::Return) {
      find_state->refresh_matches(*buffer);
      find_state->jump_to_next_match(buffer, visible);
      return true;
    }
    if (event_is_ctrl_f(event)) {
      activate_find(find_state.get(), buffer, layout_state, find_input);
      return true;
    }
    return handle_editor_keys(workspace, focus, find_state.get(), goto_state.get(),
                              completion_state.get(), symbols, file_indexer, symbol_indexer,
                              layout_state, event, visible);
  };

  if (layout_state != nullptr) {
    layout_state->editor_key_handler = dispatch_editor_keys;
  }

  return WrapFocusable(CatchEvent(panel, [dispatch_editor_keys, workspace, focus, panel_state,
                                          find_state, layout_state](Event event) {
    workspace->ensure_buffer();
    const int visible = visible_line_count(panel_state->code_box);
    if (handle_editor_mouse(&workspace->buffer, focus, find_state.get(), layout_state,
                            *panel_state, event, visible)) {
      return true;
    }
    return dispatch_editor_keys(event);
  }));
}

}  // namespace tgdb
