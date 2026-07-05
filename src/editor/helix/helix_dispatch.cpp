#include "editor/helix/helix_dispatch.hpp"

#include <algorithm>
#include <cctype>

#include "editor/clipboard.hpp"
#include "editor/bracket_match.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/helix/helix_key.hpp"
#include "editor/helix/helix_keymap.hpp"
#include "editor/helix/helix_scope_nav.hpp"
#include "editor/line_comment.hpp"
#include "editor/text_ops.hpp"
#include "editor/undo_stack.hpp"
#include "ftxui/component/event.hpp"
#include "ui/key_bindings.hpp"

namespace tgdb {

namespace {

void ensure_view(HelixDispatchContext ctx) {
  ensure_scroll_visible(ctx.buffer, ctx.visible_lines, ctx.code_width);
}

void notify(HelixDispatchContext ctx) {
  if (ctx.on_buffer_changed) {
    ctx.on_buffer_changed();
  }
}

HelixScopeNavContext scope_nav_context(const HelixDispatchContext& ctx) {
  HelixScopeNavContext nav;
  nav.buffer = ctx.buffer;
  nav.symbols = ctx.symbols;
  nav.visible_lines = ctx.visible_lines;
  return nav;
}

bool run_scope_nav(const HelixDispatchContext& ctx, bool (*fn)(const HelixScopeNavContext&)) {
  if (fn == nullptr || ctx.buffer == nullptr) {
    return false;
  }
  if (fn(scope_nav_context(ctx))) {
    ensure_view(ctx);
    notify(ctx);
  }
  return true;
}

void helix_enter_insert(HelixEditorState* helix, EditorBuffer* buffer, int col_offset) {
  if (helix == nullptr || buffer == nullptr) {
    return;
  }
  clear_primary_selection(buffer);
  helix->mode = HelixMode::kInsert;
  helix->clear_pending();
  helix->clear_count();
  if (col_offset > 0) {
    move_primary_right(buffer, false);
  }
}

void collapse_cursors_for_fresh_extend(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  for (auto& cursor : buffer->cursors) {
    cursor.collapse_to_head();
  }
}

void move_fresh_extend(EditorBuffer* buffer, void (*move_fn)(EditorBuffer*, bool)) {
  collapse_cursors_for_fresh_extend(buffer);
  move_fn(buffer, true);
}

void goto_buffer_line_fresh_extend(EditorBuffer* buffer, int line_one_based, int visible_lines,
                                   bool to_line_end) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  collapse_cursors_for_fresh_extend(buffer);
  const int max_line = static_cast<int>(buffer->lines.size());
  const int line = std::max(1, std::min(line_one_based, max_line)) - 1;
  auto& cursor = buffer->primary();
  cursor.head.line = line;
  if (to_line_end) {
    cursor.head.col = static_cast<int>(buffer->lines[static_cast<std::size_t>(line)].size());
  } else {
    cursor.head.col = 0;
  }
  clamp_all_cursors(buffer);
  if (visible_lines > 0) {
    buffer->scroll = std::max(0, line - visible_lines / 2);
  }
  ensure_scroll_visible(buffer, visible_lines);
}

bool helix_command_repeatable(HelixCommand command) {
  switch (command) {
    case HelixCommand::kMoveCharLeft:
    case HelixCommand::kMoveCharRight:
    case HelixCommand::kMoveLineUp:
    case HelixCommand::kMoveLineDown:
    case HelixCommand::kMoveWordForward:
    case HelixCommand::kMoveWordBackward:
    case HelixCommand::kMoveWordEnd:
    case HelixCommand::kGotoLineStart:
    case HelixCommand::kGotoLineEnd:
    case HelixCommand::kPageUp:
    case HelixCommand::kPageDown:
    case HelixCommand::kHalfPageUp:
    case HelixCommand::kHalfPageDown:
    case HelixCommand::kGotoFileStart:
    case HelixCommand::kGotoFileEnd:
    case HelixCommand::kExtendCharLeft:
    case HelixCommand::kExtendCharRight:
    case HelixCommand::kExtendLineUp:
    case HelixCommand::kExtendLineDown:
    case HelixCommand::kExtendWordForward:
    case HelixCommand::kExtendWordBackward:
    case HelixCommand::kExtendWordEnd:
    case HelixCommand::kExtendLineStart:
    case HelixCommand::kExtendLineEnd:
    case HelixCommand::kExtendLineBelow:
    case HelixCommand::kExtendLineBounds:
    case HelixCommand::kScrollUp:
    case HelixCommand::kScrollDown:
      return true;
    default:
      return false;
  }
}

void execute_command_buffer(const HelixDispatchContext& ctx) {
  if (ctx.helix == nullptr) {
    return;
  }
  std::string cmd = ctx.helix->command_buffer;
  while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.front()))) {
    cmd.erase(cmd.begin());
  }
  while (!cmd.empty() && std::isspace(static_cast<unsigned char>(cmd.back()))) {
    cmd.pop_back();
  }
  ctx.helix->clear_command();
  if (cmd == "w" || cmd == "write") {
    if (ctx.save_file) {
      ctx.save_file();
    }
  } else if (cmd == "q" || cmd == "quit") {
    if (ctx.request_quit) {
      ctx.request_quit();
    }
  } else if (cmd == "wq") {
    if (ctx.save_file) {
      ctx.save_file();
    }
    if (ctx.request_quit) {
      ctx.request_quit();
    }
  }
}

bool handle_command_mode_keys(const HelixDispatchContext& ctx, const ftxui::Event& event) {
  if (ctx.helix == nullptr || !ctx.helix->command_mode) {
    return false;
  }
  if (event == ftxui::Event::Escape) {
    ctx.helix->clear_command();
    return true;
  }
  if (event == ftxui::Event::Return) {
    execute_command_buffer(ctx);
    return true;
  }
  if (event == ftxui::Event::Backspace) {
    if (!ctx.helix->command_buffer.empty()) {
      ctx.helix->command_buffer.pop_back();
    }
    return true;
  }
  char ch = '\0';
  if (helix_event_is_printable(event, &ch)) {
    ctx.helix->command_buffer.push_back(ch);
    return true;
  }
  return true;
}

bool accumulate_count_digit(HelixEditorState* helix, char digit) {
  if (helix == nullptr) {
    return false;
  }
  if (digit == '0' && helix->count > 0) {
    helix->count *= 10;
  } else if (digit >= '1' && digit <= '9') {
    helix->count = helix->count * 10 + (digit - '0');
  } else if (digit == '0' && helix->count == 0) {
    helix->count = 0;
    return false;
  } else {
    return false;
  }
  return true;
}

void select_paren(EditorBuffer* buffer, bool around) {
  if (buffer == nullptr) {
    return;
  }
  const int line = buffer->primary_line();
  const int col = buffer->primary_col();
  const BracketPairHighlight pair = find_bracket_pair_highlight(*buffer, line, col);
  if (!pair.valid) {
    return;
  }
  if (pair.line_a < 0 || pair.line_a >= static_cast<int>(buffer->lines.size())) {
    return;
  }
  const std::string& open_line = buffer->lines[static_cast<std::size_t>(pair.line_a)];
  if (pair.col_a < 0 || pair.col_a >= static_cast<int>(open_line.size()) ||
      open_line[static_cast<std::size_t>(pair.col_a)] != '(') {
    return;
  }
  if (around) {
    buffer->reset_to_single_cursor(pair.line_a, pair.col_a);
    buffer->primary().anchor = {pair.line_a, pair.col_a};
    int end_col = pair.col_b + 1;
    if (pair.line_b >= 0 && pair.line_b < static_cast<int>(buffer->lines.size()) &&
        pair.col_b >= 0 &&
        pair.col_b < static_cast<int>(buffer->lines[static_cast<std::size_t>(pair.line_b)].size()) &&
        buffer->lines[static_cast<std::size_t>(pair.line_b)][static_cast<std::size_t>(pair.col_b)] ==
            ')') {
      end_col = pair.col_b + 1;
    }
    buffer->primary().head = {pair.line_b, end_col};
  } else {
    buffer->reset_to_single_cursor(pair.line_a, pair.col_a + 1);
    buffer->primary().anchor = {pair.line_a, pair.col_a + 1};
    buffer->primary().head = {pair.line_b, pair.col_b};
  }
  clamp_all_cursors(buffer);
}

void match_brackets(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  const BracketPairHighlight pair =
      find_bracket_pair_highlight(*buffer, buffer->primary_line(), buffer->primary_col());
  if (!pair.valid) {
    return;
  }
  buffer->reset_to_single_cursor(pair.line_a, pair.col_a);
  buffer->primary().anchor = {pair.line_a, pair.col_a};
  int end_col = pair.col_b + 1;
  if (pair.line_b >= 0 && pair.line_b < static_cast<int>(buffer->lines.size()) && pair.col_b >= 0 &&
      pair.col_b < static_cast<int>(buffer->lines[static_cast<std::size_t>(pair.line_b)].size())) {
    end_col = pair.col_b + 1;
  }
  buffer->primary().head = {pair.line_b, end_col};
  clamp_all_cursors(buffer);
}

void indent_lines(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  push_undo(buffer);
  const int tab = std::max(1, 4);
  for (auto& cursor : buffer->cursors) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    if (cursor.has_selection()) {
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    } else {
      start_line = end_line = cursor.head.line;
    }
    for (int line = start_line; line <= end_line; ++line) {
      if (line < 0 || line >= static_cast<int>(buffer->lines.size())) {
        continue;
      }
      buffer->lines[static_cast<std::size_t>(line)].insert(0, static_cast<std::size_t>(tab), ' ');
      for (auto& other : buffer->cursors) {
        if (other.head.line == line && other.head.col >= 0) {
          other.head.col += tab;
        }
        if (other.anchor.line == line && other.anchor.col >= 0) {
          other.anchor.col += tab;
        }
      }
    }
  }
  clamp_all_cursors(buffer);
  buffer->dirty = true;
  buffer->view_token++;
}

void unindent_lines(EditorBuffer* buffer) {
  if (buffer == nullptr || buffer->lines.empty()) {
    return;
  }
  push_undo(buffer);
  for (auto& cursor : buffer->cursors) {
    int start_line = 0;
    int start_col = 0;
    int end_line = 0;
    int end_col = 0;
    if (cursor.has_selection()) {
      cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
    } else {
      start_line = end_line = cursor.head.line;
    }
    for (int line = start_line; line <= end_line; ++line) {
      if (line < 0 || line >= static_cast<int>(buffer->lines.size())) {
        continue;
      }
      std::string& text = buffer->lines[static_cast<std::size_t>(line)];
      int removed = 0;
      while (removed < 4 && !text.empty() && text.front() == ' ') {
        text.erase(text.begin());
        ++removed;
      }
      if (removed == 0 && !text.empty() && text.front() == '\t') {
        text.erase(text.begin());
        removed = 1;
      }
      if (removed > 0) {
        for (auto& other : buffer->cursors) {
          if (other.head.line == line) {
            other.head.col = std::max(0, other.head.col - removed);
          }
          if (other.anchor.line == line) {
            other.anchor.col = std::max(0, other.anchor.col - removed);
          }
        }
      }
    }
  }
  clamp_all_cursors(buffer);
  buffer->dirty = true;
  buffer->view_token++;
}

void delete_char_at_cursor(EditorBuffer* buffer, bool forward) {
  if (buffer == nullptr) {
    return;
  }
  if (buffer->primary().has_selection()) {
    push_undo(buffer);
    delete_all_selections(buffer);
    buffer->dirty = true;
    buffer->view_token++;
    return;
  }
  if (forward) {
    delete_char(buffer);
  } else {
    backspace(buffer);
  }
}

void yank_selection(EditorBuffer* buffer, HelixEditorState* helix) {
  if (buffer == nullptr || helix == nullptr) {
    return;
  }
  if (copy_selection(buffer)) {
    helix->yank_register = editor_clipboard();
    return;
  }
  const int line = buffer->primary_line();
  if (line >= 0 && line < static_cast<int>(buffer->lines.size())) {
    helix->yank_register = buffer->lines[static_cast<std::size_t>(line)];
    editor_clipboard() = helix->yank_register;
  }
}

void paste_from_register(HelixDispatchContext ctx, bool before) {
  std::string text = ctx.helix->yank_register;
  if (text.empty()) {
    text = read_clipboard_for_paste();
    ctx.helix->yank_register = text;
  }
  if (text.empty()) {
    return;
  }
  if (before && !ctx.buffer->primary().has_selection()) {
    move_primary_left(ctx.buffer, false);
  }
  paste_at_primary(ctx.buffer, text);
  ensure_view(ctx);
  notify(ctx);
}

void select_inner_word(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  select_word_at(buffer, buffer->primary_line(), buffer->primary_col());
}

void select_around_word(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  select_word_at(buffer, buffer->primary_line(), buffer->primary_col());
  const int line = buffer->primary_line();
  if (line < 0 || line >= static_cast<int>(buffer->lines.size())) {
    return;
  }
  auto& cursor = buffer->primary();
  const std::string& text = buffer->lines[static_cast<std::size_t>(line)];
  int start = std::min(cursor.anchor.col, cursor.head.col);
  int end = std::max(cursor.anchor.col, cursor.head.col);
  if (start > 0 && std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(start - 1)]))) {
    --start;
  }
  if (end < static_cast<int>(text.size()) &&
      std::isspace(static_cast<unsigned char>(text[static_cast<std::size_t>(end)]))) {
    ++end;
  }
  cursor.anchor = {line, start};
  cursor.head = {line, end};
  clamp_cursor(&cursor, *buffer);
}

}  // namespace

bool execute_helix_command(const HelixDispatchContext& ctx, HelixCommand command) {
  if (ctx.buffer == nullptr || ctx.helix == nullptr) {
    return false;
  }
  EditorBuffer* buffer = ctx.buffer;
  HelixEditorState* helix = ctx.helix;

  switch (command) {
    case HelixCommand::kMoveCharLeft:
      move_fresh_extend(buffer, move_primary_left);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveCharRight:
      move_fresh_extend(buffer, move_primary_right);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveLineUp:
      move_fresh_extend(buffer, move_primary_up);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveLineDown:
      move_fresh_extend(buffer, move_primary_down);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveWordForward:
      move_fresh_extend(buffer, move_primary_word_right);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveWordBackward:
      move_fresh_extend(buffer, move_primary_word_left);
      ensure_view(ctx);
      return true;
    case HelixCommand::kMoveWordEnd:
      move_fresh_extend(buffer, move_primary_word_right);
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoLineStart:
      move_fresh_extend(buffer, move_primary_home);
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoLineEnd:
      move_fresh_extend(buffer, move_primary_end);
      ensure_view(ctx);
      return true;
    case HelixCommand::kPageUp:
      move_primary_page_up(buffer, ctx.visible_lines, false);
      ensure_view(ctx);
      return true;
    case HelixCommand::kPageDown:
      move_primary_page_down(buffer, ctx.visible_lines, false);
      ensure_view(ctx);
      return true;
    case HelixCommand::kHalfPageUp:
      move_primary_half_page_up(buffer, ctx.visible_lines, false);
      ensure_view(ctx);
      return true;
    case HelixCommand::kHalfPageDown:
      move_primary_half_page_down(buffer, ctx.visible_lines, false);
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoFileStart:
      goto_buffer_line_fresh_extend(buffer, 1, ctx.visible_lines, false);
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoFileEnd:
      goto_buffer_line_fresh_extend(buffer, static_cast<int>(buffer->lines.size()),
                                    ctx.visible_lines, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoLinePrompt:
      if (ctx.open_goto_line) {
        ctx.open_goto_line();
      }
      return true;
    case HelixCommand::kExtendCharLeft:
      move_primary_left(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendCharRight:
      move_primary_right(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineUp:
      move_primary_up(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineDown:
      move_primary_down(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendWordForward:
      move_primary_word_right(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendWordBackward:
      move_primary_word_left(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendWordEnd:
      move_primary_word_right(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineStart:
      move_primary_home(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineEnd:
      move_primary_end(buffer, true);
      ensure_view(ctx);
      return true;
    case HelixCommand::kInsertMode:
      clear_primary_selection(buffer);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      return true;
    case HelixCommand::kAppendMode:
      helix_enter_insert(helix, buffer, 1);
      return true;
    case HelixCommand::kInsertLineStart:
      move_primary_home(buffer, false);
      clear_primary_selection(buffer);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      ensure_view(ctx);
      return true;
    case HelixCommand::kInsertLineEnd:
      move_primary_end(buffer, false);
      clear_primary_selection(buffer);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      ensure_view(ctx);
      return true;
    case HelixCommand::kOpenBelow:
      move_primary_end(buffer, false);
      clear_primary_selection(buffer);
      newline(buffer);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kOpenAbove:
      move_primary_home(buffer, false);
      clear_primary_selection(buffer);
      newline(buffer);
      move_primary_up(buffer, false);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kInsertNewline:
      newline(buffer);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kNormalMode:
      exit_multi_cursor_mode(buffer);
      clear_primary_selection(buffer);
      helix->mode = HelixMode::kNormal;
      helix->clear_pending();
      helix->clear_count();
      helix->clear_command();
      helix->help_open = false;
      return true;
    case HelixCommand::kSelectMode:
      helix->mode = HelixMode::kSelect;
      helix->clear_pending();
      return true;
    case HelixCommand::kExitSelectMode:
      helix->mode = HelixMode::kNormal;
      helix->clear_pending();
      return true;
    case HelixCommand::kDeleteSelection:
      if (!buffer->primary().has_selection()) {
        select_word_at(buffer, buffer->primary_line(), buffer->primary_col());
      }
      if (buffer->primary().has_selection()) {
        yank_selection(buffer, helix);
      }
      push_undo(buffer);
      delete_all_selections(buffer);
      buffer->dirty = true;
      buffer->view_token++;
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kChangeSelection:
      if (!buffer->primary().has_selection()) {
        select_word_at(buffer, buffer->primary_line(), buffer->primary_col());
      }
      if (buffer->primary().has_selection()) {
        yank_selection(buffer, helix);
        push_undo(buffer);
        delete_all_selections(buffer);
        buffer->dirty = true;
        buffer->view_token++;
      } else {
        delete_char_at_cursor(buffer, true);
      }
      clear_primary_selection(buffer);
      helix->mode = HelixMode::kInsert;
      helix->clear_pending();
      helix->clear_count();
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kDeleteCharForward:
      delete_char_at_cursor(buffer, true);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kDeleteCharBackward:
      delete_char_at_cursor(buffer, false);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kYank:
      yank_selection(buffer, helix);
      return true;
    case HelixCommand::kPasteAfter:
      paste_from_register(ctx, false);
      return true;
    case HelixCommand::kPasteBefore:
      paste_from_register(ctx, true);
      return true;
    case HelixCommand::kUndo:
      undo_edit(buffer);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kRedo:
      redo_edit(buffer);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kSearch:
      if (ctx.open_find_bar) {
        ctx.open_find_bar();
      }
      return true;
    case HelixCommand::kSearchNext:
      if (ctx.find != nullptr && !ctx.find->matches.empty()) {
        ctx.find->jump_to_next_match(buffer, ctx.visible_lines);
        ensure_view(ctx);
      }
      return true;
    case HelixCommand::kSearchPrev:
      if (ctx.find != nullptr && !ctx.find->matches.empty()) {
        ctx.find->jump_to_next_match(buffer, ctx.visible_lines);
        ensure_view(ctx);
      }
      return true;
    case HelixCommand::kSelectAll:
      if (!buffer->lines.empty()) {
        buffer->reset_to_single_cursor(0, 0);
        auto& cursor = buffer->primary();
        const int last = static_cast<int>(buffer->lines.size()) - 1;
        cursor.head = {last, static_cast<int>(buffer->lines.back().size())};
      }
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineBelow:
      select_line_at(buffer, buffer->primary_line());
      ensure_view(ctx);
      return true;
    case HelixCommand::kExtendLineBounds:
      select_line_at(buffer, buffer->primary_line());
      ensure_view(ctx);
      return true;
    case HelixCommand::kIndent:
      if (helix->mode == HelixMode::kInsert) {
        insert_tab_stop(buffer);
      } else {
        indent_lines(buffer);
      }
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kUnindent:
      unindent_lines(buffer);
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kToggleComments: {
      const LineCommentStyle style = line_comment_style_for_path(buffer->path);
      if (buffer->primary().has_selection()) {
        comment_lines(buffer, style);
      } else {
        select_line_at(buffer, buffer->primary_line());
        comment_lines(buffer, style);
        clear_primary_selection(buffer);
      }
      ensure_view(ctx);
      notify(ctx);
      return true;
    }
    case HelixCommand::kScrollUp:
      scroll_view_by_lines(buffer, -1, ctx.visible_lines);
      ensure_view(ctx);
      return true;
    case HelixCommand::kScrollDown:
      scroll_view_by_lines(buffer, 1, ctx.visible_lines);
      ensure_view(ctx);
      return true;
    case HelixCommand::kShowHelp:
      helix->help_open = true;
      helix->clear_pending();
      return true;
    case HelixCommand::kGotoDefinition:
      if (ctx.go_to_symbol) {
        ctx.go_to_symbol(buffer->primary_line(), buffer->primary_col(), false);
      }
      helix->clear_pending();
      return true;
    case HelixCommand::kSelectInnerWord:
      select_inner_word(buffer);
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kSelectAroundWord:
      select_around_word(buffer);
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kDeleteInnerWord:
      select_inner_word(buffer);
      push_undo(buffer);
      delete_all_selections(buffer);
      buffer->dirty = true;
      buffer->view_token++;
      helix->clear_pending();
      ensure_view(ctx);
      notify(ctx);
      return true;
    case HelixCommand::kSelectInnerParen:
      select_paren(buffer, false);
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kSelectAroundParen:
      select_paren(buffer, true);
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kMatchBrackets:
      match_brackets(buffer);
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kCommandMode:
      helix->command_mode = true;
      helix->command_buffer.clear();
      helix->clear_pending();
      return true;
    case HelixCommand::kOpenQuickFile:
      if (ctx.open_quick_file) {
        ctx.open_quick_file();
      }
      helix->clear_pending();
      return true;
    case HelixCommand::kOpenSymbolPicker:
      if (ctx.open_symbol_picker) {
        ctx.open_symbol_picker();
      }
      helix->clear_pending();
      return true;
    case HelixCommand::kGotoNextDiagnostic:
      if (ctx.goto_next_diagnostic) {
        ctx.goto_next_diagnostic();
      }
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoPrevDiagnostic:
      if (ctx.goto_prev_diagnostic) {
        ctx.goto_prev_diagnostic();
      }
      helix->clear_pending();
      ensure_view(ctx);
      return true;
    case HelixCommand::kGotoNextFunction:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_next_function);
    case HelixCommand::kGotoPrevFunction:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_prev_function);
    case HelixCommand::kGotoNextType:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_next_type);
    case HelixCommand::kGotoPrevType:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_prev_type);
    case HelixCommand::kGotoNextParagraph:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_next_paragraph);
    case HelixCommand::kGotoPrevParagraph:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_prev_paragraph);
    case HelixCommand::kGotoBlockEnd:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_block_end);
    case HelixCommand::kGotoBlockStart:
      helix->clear_pending();
      return run_scope_nav(ctx, helix_goto_block_start);
    case HelixCommand::kNone:
    default:
      return false;
  }
}

bool dispatch_helix_keys(const HelixDispatchContext& ctx, const ftxui::Event& event) {
  if (ctx.buffer == nullptr || ctx.helix == nullptr) {
    return false;
  }

  if (event_is_tide_global_shortcut(event) || event_is_ctrl_key_release(event)) {
    return false;
  }

  if (ctx.helix->help_open) {
    if (event == ftxui::Event::Escape) {
      ctx.helix->help_open = false;
      return true;
    }
    return true;
  }

  if (handle_command_mode_keys(ctx, event)) {
    sync_helix_layout_status(ctx.layout_state, ctx.helix, true);
    return true;
  }

  if (event == ftxui::Event::Escape) {
    if (ctx.helix->prefix_active()) {
      ctx.helix->clear_pending();
      return true;
    }
    if (ctx.helix->count > 0) {
      ctx.helix->clear_count();
      return true;
    }
    if (ctx.helix->mode != HelixMode::kNormal) {
      return execute_helix_command(ctx, HelixCommand::kNormalMode);
    }
    return false;
  }

  if (event_has_ctrl_modifier(event)) {
    return false;
  }

  if (ctx.helix->mode == HelixMode::kInsert) {
    char ch = '\0';
    if (helix_event_is_printable(event, &ch)) {
      insert_char(ctx.buffer, ch);
      ensure_view(ctx);
      notify(ctx);
      return true;
    }
  }

  if ((ctx.helix->mode == HelixMode::kNormal || ctx.helix->mode == HelixMode::kSelect) &&
      !ctx.helix->prefix_active()) {
    char digit = '\0';
    if (helix_event_is_printable(event, &digit) && std::isdigit(static_cast<unsigned char>(digit))) {
      if (accumulate_count_digit(ctx.helix, digit)) {
        sync_helix_layout_status(ctx.layout_state, ctx.helix, true);
        return true;
      }
    }
  }

  const auto token = helix_key_token(event);
  if (!token.has_value()) {
    if (ctx.helix->prefix_active()) {
      ctx.helix->clear_pending();
      return true;
    }
    return ctx.helix->mode != HelixMode::kInsert;
  }

  const HelixKeyLookupResult lookup =
      helix_lookup_key(ctx.helix->mode, ctx.helix->pending_keys, *token);
  if (lookup.kind == HelixKeyLookupResult::Kind::kNone) {
    const bool had_prefix = ctx.helix->prefix_active();
    ctx.helix->clear_pending();
    if (ctx.helix->mode != HelixMode::kInsert || had_prefix) {
      return true;
    }
    return false;
  }

  ctx.helix->pending_keys.push_back(*token);

  if (lookup.kind == HelixKeyLookupResult::Kind::kPending) {
    ctx.helix->hint_visible = true;
    return true;
  }

  const HelixCommand command = lookup.command;
  ctx.helix->clear_pending();
  ctx.helix->hint_visible = false;

  int reps = std::max(1, ctx.helix->count);
  if (!helix_command_repeatable(command)) {
    reps = 1;
  }
  ctx.helix->clear_count();

  bool handled = false;
  for (int i = 0; i < reps; ++i) {
    handled = execute_helix_command(ctx, command) || handled;
  }
  sync_helix_layout_status(ctx.layout_state, ctx.helix, true);
  return handled;
}

HelixStatusSnapshot helix_status_snapshot(const HelixEditorState* helix, bool enabled) {
  HelixStatusSnapshot snap;
  if (!enabled || helix == nullptr) {
    return snap;
  }
  snap.active = true;
  snap.mode = helix->mode_label();
  snap.pending = helix->pending_label();
  return snap;
}

int helix_gutter_width(int total_lines, int visible_lines, bool relative) {
  if (!relative) {
    const int digits = std::max(1, static_cast<int>(std::to_string(total_lines).size()));
    return digits + 1;
  }
  const int max_rel = std::max(1, visible_lines);
  const int digits = std::max(1, static_cast<int>(std::to_string(max_rel).size()));
  return digits + 1;
}

std::string helix_format_line_number(int line_idx, int primary_line, int width, bool relative) {
  int display = line_idx + 1;
  if (relative) {
    display = (line_idx == primary_line) ? 0 : std::abs(line_idx - primary_line);
  }
  std::string text = std::to_string(display);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

void sync_helix_layout_status(MainLayoutState* layout_state, const HelixEditorState* helix,
                              bool enabled) {
  if (layout_state == nullptr) {
    return;
  }
  const HelixStatusSnapshot snap = helix_status_snapshot(helix, enabled);
  layout_state->helix_status = snap;
  layout_state->editor_helix_prefix_pending =
      enabled && helix != nullptr &&
      (helix->prefix_active() || helix->command_mode);
}

}  // namespace tgdb
