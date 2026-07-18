#include "editor/helix/helix_textobjects.hpp"

#include <algorithm>
#include <cctype>
#include <climits>

#include "editor/bracket_match.hpp"
#include "editor/editor_context.hpp"
#include "editor/text_ops.hpp"

namespace tuide {

namespace {

bool is_function_kind(SymbolKind kind) {
  return kind == SymbolKind::kFunction || kind == SymbolKind::kMethod;
}

bool is_type_kind(SymbolKind kind) {
  return kind == SymbolKind::kNamespace || kind == SymbolKind::kClass ||
         kind == SymbolKind::kStruct;
}

std::size_t symbol_index(const std::vector<SymbolInfo>& symbols, const SymbolInfo* target) {
  for (std::size_t i = 0; i < symbols.size(); ++i) {
    if (&symbols[i] == target) {
      return i;
    }
  }
  return symbols.size();
}

int line_end_col(const EditorBuffer& buffer, int line) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  return static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
}

void apply_line_range(EditorBuffer* buffer, int start_line, int start_col, int end_line,
                      int end_col) {
  if (buffer == nullptr) {
    return;
  }
  start_line = std::max(0, std::min(start_line, static_cast<int>(buffer->lines.size()) - 1));
  end_line = std::max(0, std::min(end_line, static_cast<int>(buffer->lines.size()) - 1));
  start_col = std::max(0, std::min(start_col, line_end_col(*buffer, start_line)));
  end_col = std::max(0, std::min(end_col, line_end_col(*buffer, end_line)));
  buffer->reset_to_single_cursor(start_line, start_col);
  buffer->primary().anchor = {start_line, start_col};
  buffer->primary().head = {end_line, end_col};
  clamp_all_cursors(buffer);
}

void apply_inner_brace_range(EditorBuffer* buffer, const BracketPairHighlight& body) {
  if (buffer == nullptr || !body.valid) {
    return;
  }
  const int open_line =
      std::max(0, std::min(body.line_a, static_cast<int>(buffer->lines.size()) - 1));
  const int open_col = std::max(0, std::min(body.col_a, line_end_col(*buffer, open_line)));
  const int end_line =
      std::max(0, std::min(body.line_b, static_cast<int>(buffer->lines.size()) - 1));
  const int end_col = std::max(0, std::min(body.col_b, line_end_col(*buffer, end_line)));
  const int inner_start_col =
      std::min(open_col + 1, line_end_col(*buffer, open_line));
  buffer->reset_to_single_cursor(open_line, inner_start_col);
  buffer->primary().anchor = {end_line, end_col};
  buffer->primary().head = {open_line, inner_start_col};
  clamp_all_cursors(buffer);
}

BracketPairHighlight first_brace_pair_in_range(const EditorBuffer& buffer, int start_line,
                                               int end_line) {
  BracketPairHighlight result;
  if (buffer.lines.empty()) {
    return result;
  }
  start_line = std::max(0, start_line);
  end_line = std::min(end_line, static_cast<int>(buffer.lines.size()) - 1);
  for (int line = start_line; line <= end_line; ++line) {
    const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
    for (int col = 0; col < static_cast<int>(text.size()); ++col) {
      if (text[static_cast<std::size_t>(col)] != '{') {
        continue;
      }
      if (!cursor_in_code(buffer, line, col)) {
        continue;
      }
      const BracketPairHighlight found = find_enclosing_bracket_pair(buffer, line, col, '{');
      if (found.valid && found.line_a >= start_line && found.line_a <= end_line) {
        return found;
      }
    }
  }
  return result;
}

bool brace_pair_in_symbol_range(const BracketPairHighlight& pair, int start_line, int end_line) {
  return pair.valid && pair.line_a >= start_line && pair.line_a <= end_line;
}

BracketPairHighlight pick_symbol_brace_body(const EditorBuffer& buffer, int line, int col,
                                            int start_line, int end_line) {
  int nav_line = line;
  int nav_col = col;

  const BracketPairHighlight enclosing = find_enclosing_bracket_pair(buffer, line, col, '{');
  if (enclosing.valid && line == enclosing.line_a && col == enclosing.col_a + 1) {
    nav_line = enclosing.line_a;
    nav_col = enclosing.col_a;
  }

  BracketPairHighlight body =
      find_enclosing_bracket_pair_for_block_nav(buffer, nav_line, nav_col, '{', true);
  if (brace_pair_in_symbol_range(body, start_line, end_line)) {
    return body;
  }

  body = find_enclosing_bracket_pair(buffer, line, col, '{');
  if (brace_pair_in_symbol_range(body, start_line, end_line)) {
    return body;
  }

  return first_brace_pair_in_range(buffer, start_line, end_line);
}

bool select_symbol_scope(const HelixTextObjectContext& ctx, bool around,
                         bool (*predicate)(SymbolKind)) {
  if (ctx.buffer == nullptr || ctx.symbols == nullptr || ctx.buffer->path.empty()) {
    return false;
  }
  const int line = ctx.buffer->primary_line();
  const int col = ctx.buffer->primary_col();
  const std::vector<SymbolInfo> symbols = ctx.symbols->symbols_for_file(ctx.buffer->path);
  const SymbolInfo* sym = innermost_scope_symbol(symbols, line, predicate);
  if (sym == nullptr) {
    return false;
  }
  const std::size_t index = symbol_index(symbols, sym);
  if (index >= symbols.size()) {
    return false;
  }
  const int end_line_1 = symbol_end_line_1based(symbols, index);
  const int start_line = std::max(0, sym->line - 1);
  const int end_line = end_line_1 == INT_MAX
                           ? static_cast<int>(ctx.buffer->lines.size()) - 1
                           : std::max(start_line, end_line_1 - 1);

  if (around) {
    apply_line_range(ctx.buffer, start_line, 0, end_line, line_end_col(*ctx.buffer, end_line));
    return true;
  }

  const BracketPairHighlight body =
      pick_symbol_brace_body(*ctx.buffer, line, col, start_line, end_line);
  if (body.valid) {
    apply_inner_brace_range(ctx.buffer, body);
    return true;
  }

  if (start_line == end_line) {
    apply_line_range(ctx.buffer, start_line, 0, end_line, line_end_col(*ctx.buffer, end_line));
    return true;
  }
  apply_line_range(ctx.buffer, start_line + 1, 0, end_line,
                   line_end_col(*ctx.buffer, end_line));
  return true;
}

struct ArgPiece {
  int start_line = 0;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
};

bool cursor_in_piece(int line, int col, const ArgPiece& piece) {
  if (line < piece.start_line || line > piece.end_line) {
    return false;
  }
  if (line == piece.start_line && col < piece.start_col) {
    return false;
  }
  if (line == piece.end_line && col > piece.end_col) {
    return false;
  }
  return true;
}

void trim_arg_piece(const EditorBuffer& buffer, ArgPiece* piece) {
  if (piece == nullptr) {
    return;
  }
  if (piece->start_line == piece->end_line) {
    const std::string& line = buffer.lines[static_cast<std::size_t>(piece->start_line)];
    while (piece->start_col < piece->end_col &&
           piece->start_col < static_cast<int>(line.size()) &&
           std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(piece->start_col)]))) {
      ++piece->start_col;
    }
    while (piece->end_col > piece->start_col &&
           piece->end_col > 0 &&
           std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(piece->end_col - 1)]))) {
      --piece->end_col;
    }
    return;
  }
  while (piece->start_line <= piece->end_line) {
    const std::string& line = buffer.lines[static_cast<std::size_t>(piece->start_line)];
    if (piece->start_col < static_cast<int>(line.size()) &&
        !std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(piece->start_col)]))) {
      break;
    }
    if (piece->start_col + 1 < static_cast<int>(line.size())) {
      ++piece->start_col;
    } else {
      ++piece->start_line;
      piece->start_col = 0;
    }
  }
  while (piece->end_line >= piece->start_line) {
    const std::string& line = buffer.lines[static_cast<std::size_t>(piece->end_line)];
    if (piece->end_col > 0 &&
        !std::isspace(
            static_cast<unsigned char>(line[static_cast<std::size_t>(piece->end_col - 1)]))) {
      break;
    }
    if (piece->end_col > 0) {
      --piece->end_col;
    } else if (piece->end_line > piece->start_line) {
      --piece->end_line;
      piece->end_col = line_end_col(buffer, piece->end_line);
    } else {
      break;
    }
  }
}

bool select_argument(const HelixTextObjectContext& ctx, bool around) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const int line = ctx.buffer->primary_line();
  const int col = ctx.buffer->primary_col();
  const BracketPairHighlight paren = find_enclosing_bracket_pair(*ctx.buffer, line, col, '(');
  if (!paren.valid) {
    return false;
  }

  int depth_paren = 0;
  int depth_square = 0;
  int depth_brace = 0;
  bool in_string = false;
  bool in_char = false;
  char string_quote = '\0';

  ArgPiece current;
  current.start_line = paren.line_a;
  current.start_col = paren.col_a + 1;
  current.end_line = paren.line_a;
  current.end_col = paren.col_a + 1;

  const auto flush_arg = [&](const ArgPiece& piece) -> bool {
    if (!cursor_in_piece(line, col, piece)) {
      return false;
    }
    ArgPiece trimmed = piece;
    if (around) {
      apply_line_range(ctx.buffer, trimmed.start_line, trimmed.start_col, trimmed.end_line,
                       trimmed.end_col);
      return true;
    }
    trim_arg_piece(*ctx.buffer, &trimmed);
    apply_line_range(ctx.buffer, trimmed.start_line, trimmed.start_col, trimmed.end_line,
                     trimmed.end_col);
    return true;
  };

  int scan_line = paren.line_a;
  int scan_col = paren.col_a + 1;
  const int stop_line = paren.line_b;
  const int stop_col = paren.col_b;

  while (scan_line < stop_line || (scan_line == stop_line && scan_col < stop_col)) {
    if (scan_line < 0 || scan_line >= static_cast<int>(ctx.buffer->lines.size())) {
      break;
    }
    const std::string& text = ctx.buffer->lines[static_cast<std::size_t>(scan_line)];
    if (scan_col >= static_cast<int>(text.size())) {
      ++scan_line;
      scan_col = 0;
      continue;
    }
    const char ch = text[static_cast<std::size_t>(scan_col)];
    const char next =
        scan_col + 1 < static_cast<int>(text.size())
            ? text[static_cast<std::size_t>(scan_col + 1)]
            : '\0';

    if (!in_string && !in_char) {
      if (ch == '/' && next == '/') {
        scan_col = static_cast<int>(text.size());
        continue;
      }
      if (ch == '/' && next == '*') {
        ++scan_col;
        while (scan_line < stop_line || (scan_line == stop_line && scan_col < stop_col)) {
          if (scan_col + 1 < static_cast<int>(text.size()) &&
              text[static_cast<std::size_t>(scan_col)] == '*' &&
              text[static_cast<std::size_t>(scan_col + 1)] == '/') {
            scan_col += 2;
            break;
          }
          if (scan_col + 1 >= static_cast<int>(text.size())) {
            ++scan_line;
            scan_col = 0;
          } else {
            ++scan_col;
          }
        }
        continue;
      }
      if (ch == '"') {
        in_string = true;
        string_quote = '"';
      } else if (ch == '\'') {
        in_char = true;
        string_quote = '\'';
      } else if (ch == '(') {
        ++depth_paren;
      } else if (ch == ')') {
        if (depth_paren > 0) {
          --depth_paren;
        }
      } else if (ch == '[') {
        ++depth_square;
      } else if (ch == ']') {
        if (depth_square > 0) {
          --depth_square;
        }
      } else if (ch == '{') {
        ++depth_brace;
      } else if (ch == '}') {
        if (depth_brace > 0) {
          --depth_brace;
        }
      } else if (ch == ',' && depth_paren == 0 && depth_square == 0 && depth_brace == 0) {
        current.end_line = scan_line;
        current.end_col = scan_col;
        if (flush_arg(current)) {
          return true;
        }
        current.start_line = scan_line;
        current.start_col = scan_col + 1;
      }
    } else {
      if (ch == string_quote && (scan_col == 0 || text[static_cast<std::size_t>(scan_col - 1)] != '\\')) {
        in_string = false;
        in_char = false;
        string_quote = '\0';
      }
    }

    current.end_line = scan_line;
    current.end_col = scan_col + 1;
    if (scan_col + 1 >= static_cast<int>(text.size())) {
      ++scan_line;
      scan_col = 0;
    } else {
      ++scan_col;
    }
  }

  return flush_arg(current);
}

TextSpan comment_span(const EditorBuffer& buffer, int line, int col) {
  TextSpan block = find_enclosing_block_comment(buffer, line, col);
  if (block.valid) {
    return block;
  }
  return find_enclosing_line_comment(buffer, line, col);
}

}  // namespace

void helix_apply_delimited_selection(EditorBuffer* buffer, const TextSpan& span, bool around,
                                     int inner_skip_start, int inner_skip_end) {
  if (buffer == nullptr || !span.valid) {
    return;
  }
  if (around) {
    apply_line_range(buffer, span.line_a, span.col_a, span.line_b, span.col_b + 1);
    return;
  }
  apply_line_range(buffer, span.line_a, span.col_a + inner_skip_start, span.line_b,
                   std::max(span.col_a + inner_skip_start, span.col_b - inner_skip_end));
}

bool helix_select_inner_function(const HelixTextObjectContext& ctx) {
  return select_symbol_scope(ctx, false, is_function_kind);
}

bool helix_select_around_function(const HelixTextObjectContext& ctx) {
  return select_symbol_scope(ctx, true, is_function_kind);
}

bool helix_select_inner_type(const HelixTextObjectContext& ctx) {
  return select_symbol_scope(ctx, false, is_type_kind);
}

bool helix_select_around_type(const HelixTextObjectContext& ctx) {
  return select_symbol_scope(ctx, true, is_type_kind);
}

bool helix_select_inner_argument(const HelixTextObjectContext& ctx) {
  return select_argument(ctx, false);
}

bool helix_select_around_argument(const HelixTextObjectContext& ctx) {
  return select_argument(ctx, true);
}

bool helix_select_inner_comment(const HelixTextObjectContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const int line = ctx.buffer->primary_line();
  const int col = ctx.buffer->primary_col();
  const TextSpan block = find_enclosing_block_comment(*ctx.buffer, line, col);
  if (block.valid) {
    helix_apply_delimited_selection(ctx.buffer, block, false, 2, 2);
    return true;
  }
  const TextSpan line_comment = find_enclosing_line_comment(*ctx.buffer, line, col);
  if (!line_comment.valid) {
    return false;
  }
  helix_apply_delimited_selection(ctx.buffer, line_comment, false, 2, 0);
  return true;
}

bool helix_select_around_comment(const HelixTextObjectContext& ctx) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const int line = ctx.buffer->primary_line();
  const int col = ctx.buffer->primary_col();
  const TextSpan span = comment_span(*ctx.buffer, line, col);
  if (!span.valid) {
    return false;
  }
  helix_apply_delimited_selection(ctx.buffer, span, true, 0, 0);
  return true;
}

bool helix_select_inner_quote(const HelixTextObjectContext& ctx, char quote_ch) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const TextSpan span =
      find_enclosing_quote_pair(*ctx.buffer, ctx.buffer->primary_line(),
                                ctx.buffer->primary_col(), quote_ch);
  if (!span.valid) {
    return false;
  }
  helix_apply_delimited_selection(ctx.buffer, span, false, 1, 0);
  return true;
}

bool helix_select_around_quote(const HelixTextObjectContext& ctx, char quote_ch) {
  if (ctx.buffer == nullptr) {
    return false;
  }
  const TextSpan span =
      find_enclosing_quote_pair(*ctx.buffer, ctx.buffer->primary_line(),
                                ctx.buffer->primary_col(), quote_ch);
  if (!span.valid) {
    return false;
  }
  helix_apply_delimited_selection(ctx.buffer, span, true, 0, 0);
  return true;
}

}  // namespace tuide
