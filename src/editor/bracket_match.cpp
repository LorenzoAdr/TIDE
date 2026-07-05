#include "editor/bracket_match.hpp"

#include <algorithm>

namespace tgdb {

namespace {

bool is_bracket(char c) {
  return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

bool is_opening(char c) {
  return c == '(' || c == '[' || c == '{';
}

char closing_for(char open) {
  switch (open) {
    case '(':
      return ')';
    case '[':
      return ']';
    case '{':
      return '}';
    default:
      return '\0';
  }
}

char opening_for(char close) {
  switch (close) {
    case ')':
      return '(';
    case ']':
      return '[';
    case '}':
      return '{';
    default:
      return '\0';
  }
}

struct TextPos {
  int line = 0;
  int col = 0;

  bool operator==(const TextPos& other) const {
    return line == other.line && col == other.col;
  }
};

int line_len(const EditorBuffer& buffer, int line) {
  if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return 0;
  }
  return static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
}

char char_at(const EditorBuffer& buffer, TextPos pos) {
  if (pos.line < 0 || pos.line >= static_cast<int>(buffer.lines.size())) {
    return '\0';
  }
  const int len = line_len(buffer, pos.line);
  if (pos.col < 0 || pos.col >= len) {
    return '\0';
  }
  return buffer.lines[static_cast<std::size_t>(pos.line)][static_cast<std::size_t>(pos.col)];
}

char peek_next(const EditorBuffer& buffer, TextPos pos) {
  if (pos.line < 0) {
    return '\0';
  }
  const int len = line_len(buffer, pos.line);
  if (pos.col + 1 < len) {
    return buffer.lines[static_cast<std::size_t>(pos.line)][static_cast<std::size_t>(pos.col + 1)];
  }
  if (pos.line + 1 < static_cast<int>(buffer.lines.size())) {
    const auto& next = buffer.lines[static_cast<std::size_t>(pos.line + 1)];
    return next.empty() ? '\0' : next[0];
  }
  return '\0';
}

TextPos normalize_forward(const EditorBuffer& buffer, TextPos pos) {
  while (pos.line >= 0 && pos.line < static_cast<int>(buffer.lines.size())) {
    const int len = line_len(buffer, pos.line);
    if (len == 0 || pos.col >= len) {
      if (pos.line + 1 < static_cast<int>(buffer.lines.size())) {
        pos = {pos.line + 1, 0};
        continue;
      }
      return {-1, -1};
    }
    return pos;
  }
  return {-1, -1};
}

TextPos advance(const EditorBuffer& buffer, TextPos pos) {
  pos = normalize_forward(buffer, pos);
  if (pos.line < 0) {
    return pos;
  }

  const int len = line_len(buffer, pos.line);
  if (pos.col + 1 < len) {
    return {pos.line, pos.col + 1};
  }
  if (pos.line + 1 < static_cast<int>(buffer.lines.size())) {
    return {pos.line + 1, 0};
  }
  return {-1, -1};
}

TextPos retreat(const EditorBuffer& buffer, TextPos pos) {
  if (pos.line < 0) {
    return pos;
  }
  while (pos.line >= 0) {
    if (pos.col > 0) {
      return {pos.line, pos.col - 1};
    }
    if (pos.line == 0) {
      return {-1, -1};
    }
    pos.line -= 1;
    const int len = line_len(buffer, pos.line);
    if (len > 0) {
      return {pos.line, len - 1};
    }
    pos.col = 0;
  }
  return {-1, -1};
}

enum class LexMode { Code, LineComment, BlockComment, String, Char };

bool is_escaped(const EditorBuffer& buffer, TextPos pos) {
  int backslashes = 0;
  TextPos scan = pos;
  while (true) {
    scan = retreat(buffer, scan);
    if (scan.line < 0 || char_at(buffer, scan) != '\\') {
      break;
    }
    ++backslashes;
  }
  return backslashes % 2 == 1;
}

TextPos skip_literal_forward(const EditorBuffer& buffer, TextPos pos, LexMode* mode) {
  pos = normalize_forward(buffer, pos);
  if (pos.line < 0) {
    return pos;
  }

  const char c = char_at(buffer, pos);
  const char next = peek_next(buffer, pos);
  const int len = line_len(buffer, pos.line);

  if (*mode == LexMode::Code) {
    if (c == '"') {
      *mode = LexMode::String;
      return advance(buffer, pos);
    }
    if (c == '\'') {
      *mode = LexMode::Char;
      return advance(buffer, pos);
    }
    if (c == '/' && next == '/') {
      *mode = LexMode::LineComment;
      return advance(buffer, advance(buffer, pos));
    }
    if (c == '/' && next == '*') {
      *mode = LexMode::BlockComment;
      return advance(buffer, advance(buffer, pos));
    }
    return pos;
  }

  if (*mode == LexMode::LineComment) {
    if (len == 0 || pos.col >= len - 1) {
      *mode = LexMode::Code;
    }
    return pos;
  }

  if (*mode == LexMode::BlockComment) {
    if (c == '*' && next == '/') {
      *mode = LexMode::Code;
      TextPos after = advance(buffer, pos);
      return advance(buffer, after);
    }
    return pos;
  }

  if (*mode == LexMode::String || *mode == LexMode::Char) {
    const char quote = *mode == LexMode::String ? '"' : '\'';
    if (c == quote && !is_escaped(buffer, pos)) {
      *mode = LexMode::Code;
    }
    return pos;
  }

  return pos;
}

TextPos scan_forward_match(const EditorBuffer& buffer, TextPos start, char open_ch, char close_ch) {
  int depth = 1;
  LexMode mode = LexMode::Code;
  TextPos pos = advance(buffer, start);

  while (pos.line >= 0) {
    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    if (mode != LexMode::Code) {
      pos = advance(buffer, pos);
      continue;
    }

    const char c = char_at(buffer, pos);
    if (c == open_ch) {
      ++depth;
    } else if (c == close_ch) {
      --depth;
      if (depth == 0) {
        return pos;
      }
    }
    pos = advance(buffer, pos);
  }
  return {-1, -1};
}

TextPos scan_closing_match(const EditorBuffer& buffer, TextPos closing_pos, char open_ch,
                           char close_ch) {
  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  TextPos open_match{-1, -1};
  int depth = 0;

  while (pos.line >= 0) {
    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    if (mode != LexMode::Code) {
      pos = advance(buffer, pos);
      continue;
    }

    const char c = char_at(buffer, pos);
    if (c == open_ch) {
      if (depth == 0) {
        open_match = pos;
      }
      ++depth;
    } else if (c == close_ch && depth > 0) {
      --depth;
      if (depth == 0) {
        if (pos == closing_pos) {
          return open_match;
        }
        open_match = {-1, -1};
      }
    }
    pos = advance(buffer, pos);
  }
  return {-1, -1};
}

bool bracket_at_cursor(const EditorBuffer& buffer, int line, int col, TextPos* bracket_pos,
                       char* bracket_ch) {
  if (buffer.lines.empty()) {
    return false;
  }
  const int clamped_line =
      std::max(0, std::min(line, static_cast<int>(buffer.lines.size()) - 1));
  const std::string& text = buffer.lines[static_cast<std::size_t>(clamped_line)];
  const int len = static_cast<int>(text.size());

  if (col > 0 && col - 1 < len && is_bracket(text[static_cast<std::size_t>(col - 1)])) {
    *bracket_pos = {clamped_line, col - 1};
    *bracket_ch = text[static_cast<std::size_t>(col - 1)];
    return true;
  }
  if (col < len && is_bracket(text[static_cast<std::size_t>(col)])) {
    *bracket_pos = {clamped_line, col};
    *bracket_ch = text[static_cast<std::size_t>(col)];
    return true;
  }
  return false;
}

bool bracket_in_code(const EditorBuffer& buffer, TextPos bracket_pos) {
  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  while (pos.line >= 0) {
    if (pos == bracket_pos) {
      return mode == LexMode::Code;
    }
    if (pos.line > bracket_pos.line ||
        (pos.line == bracket_pos.line && pos.col > bracket_pos.col)) {
      return false;
    }

    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    pos = advance(buffer, pos);
  }
  return false;
}

bool cursor_position_in_code(const EditorBuffer& buffer, int line, int col) {
  if (buffer.lines.empty() || line < 0 || col < 0) {
    return false;
  }
  const TextPos target{line, col};
  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  while (pos.line >= 0) {
    if (pos.line > target.line || (pos.line == target.line && pos.col >= target.col)) {
      return mode == LexMode::Code;
    }
    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    pos = advance(buffer, pos);
  }
  return mode == LexMode::Code;
}

}  // namespace

bool cursor_in_code(const EditorBuffer& buffer, int line, int col) {
  return cursor_position_in_code(buffer, line, col);
}

BracketPairHighlight find_bracket_pair_highlight(const EditorBuffer& buffer, int line, int col) {
  BracketPairHighlight result;
  TextPos bracket_pos;
  char bracket_ch = '\0';
  if (!bracket_at_cursor(buffer, line, col, &bracket_pos, &bracket_ch)) {
    return result;
  }
  if (!bracket_in_code(buffer, bracket_pos)) {
    return result;
  }

  TextPos match_pos{-1, -1};
  if (is_opening(bracket_ch)) {
    match_pos =
        scan_forward_match(buffer, bracket_pos, bracket_ch, closing_for(bracket_ch));
  } else {
    match_pos = scan_closing_match(buffer, bracket_pos, opening_for(bracket_ch), bracket_ch);
  }

  if (match_pos.line < 0) {
    return result;
  }

  result.valid = true;
  result.line_a = bracket_pos.line;
  result.col_a = bracket_pos.col;
  result.line_b = match_pos.line;
  result.col_b = match_pos.col;
  return result;
}

bool pos_before_or_at(int line, int col, int other_line, int other_col) {
  return line < other_line || (line == other_line && col <= other_col);
}

bool pos_after_or_at(int line, int col, int other_line, int other_col) {
  return line > other_line || (line == other_line && col >= other_col);
}

BracketPairHighlight find_enclosing_bracket_pair(const EditorBuffer& buffer, int line, int col,
                                                 char open_ch) {
  BracketPairHighlight result;
  if (buffer.lines.empty()) {
    return result;
  }
  const char close_ch = closing_for(open_ch);
  if (close_ch == '\0') {
    return result;
  }

  BracketPairHighlight best;
  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  while (pos.line >= 0) {
    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    if (mode == LexMode::Code && char_at(buffer, pos) == open_ch &&
        pos_before_or_at(pos.line, pos.col, line, col)) {
      const TextPos close = scan_forward_match(buffer, pos, open_ch, close_ch);
      if (close.line >= 0 && pos_after_or_at(close.line, close.col, line, col)) {
        if (!best.valid || pos.line > best.line_a ||
            (pos.line == best.line_a && pos.col > best.col_a)) {
          best.valid = true;
          best.line_a = pos.line;
          best.col_a = pos.col;
          best.line_b = close.line;
          best.col_b = close.col;
        }
      }
    }
    pos = advance(buffer, pos);
  }
  return best;
}

BracketPairHighlight find_innermost_enclosing_pair(const EditorBuffer& buffer, int line, int col) {
  BracketPairHighlight best;
  for (const char open_ch : {'(', '[', '{'}) {
    const BracketPairHighlight pair = find_enclosing_bracket_pair(buffer, line, col, open_ch);
    if (!pair.valid) {
      continue;
    }
    if (!best.valid || pair.line_a > best.line_a ||
        (pair.line_a == best.line_a && pair.col_a > best.col_a)) {
      best = pair;
    }
  }
  return best;
}

std::vector<BracketPairHighlight> collect_enclosing_bracket_pairs(const EditorBuffer& buffer,
                                                                  int line, int col,
                                                                  char open_ch) {
  std::vector<BracketPairHighlight> pairs;
  if (buffer.lines.empty()) {
    return pairs;
  }
  const char close_ch = closing_for(open_ch);
  if (close_ch == '\0') {
    return pairs;
  }

  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  while (pos.line >= 0) {
    pos = skip_literal_forward(buffer, pos, &mode);
    if (pos.line < 0) {
      break;
    }
    if (mode == LexMode::Code && char_at(buffer, pos) == open_ch &&
        pos_before_or_at(pos.line, pos.col, line, col)) {
      const TextPos close = scan_forward_match(buffer, pos, open_ch, close_ch);
      if (close.line >= 0 && pos_after_or_at(close.line, close.col, line, col)) {
        BracketPairHighlight pair;
        pair.valid = true;
        pair.line_a = pos.line;
        pair.col_a = pos.col;
        pair.line_b = close.line;
        pair.col_b = close.col;
        const auto same_open = [&pair](const BracketPairHighlight& other) {
          return other.line_a == pair.line_a && other.col_a == pair.col_a;
        };
        if (std::none_of(pairs.begin(), pairs.end(), same_open)) {
          pairs.push_back(pair);
        }
      }
    }
    pos = advance(buffer, pos);
  }

  std::sort(pairs.begin(), pairs.end(),
            [](const BracketPairHighlight& a, const BracketPairHighlight& b) {
              if (a.line_a != b.line_a) {
                return a.line_a < b.line_a;
              }
              return a.col_a < b.col_a;
            });
  return pairs;
}

BracketPairHighlight find_enclosing_bracket_pair_for_block_nav(const EditorBuffer& buffer,
                                                               int line, int col, char open_ch,
                                                               bool jump_to_start) {
  const std::vector<BracketPairHighlight> pairs =
      collect_enclosing_bracket_pairs(buffer, line, col, open_ch);
  if (pairs.empty()) {
    return {};
  }

  const int inner_idx = static_cast<int>(pairs.size()) - 1;
  const BracketPairHighlight& inner = pairs[static_cast<std::size_t>(inner_idx)];
  if (jump_to_start) {
    if (line == inner.line_a && col == inner.col_a && inner_idx > 0) {
      return pairs[static_cast<std::size_t>(inner_idx - 1)];
    }
    return inner;
  }
  if (line == inner.line_b && col == inner.col_b && inner_idx > 0) {
    return pairs[static_cast<std::size_t>(inner_idx - 1)];
  }
  return inner;
}

TextSpan find_enclosing_quote_pair(const EditorBuffer& buffer, int line, int col, char quote_ch) {
  TextSpan result;
  if (buffer.lines.empty() || quote_ch == '\0') {
    return result;
  }

  enum class ScanMode { Code, LineComment, BlockComment, Double, Single, Backtick, Target };
  ScanMode mode = ScanMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  TextPos open{-1, -1};
  TextSpan best;

  while (pos.line >= 0) {
    const char c = char_at(buffer, pos);
    const char next = peek_next(buffer, pos);
    const int len = line_len(buffer, pos.line);

    if (mode == ScanMode::Code) {
      if (c == '/' && next == '/') {
        mode = ScanMode::LineComment;
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      if (c == '/' && next == '*') {
        mode = ScanMode::BlockComment;
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      if (c == '"') {
        mode = ScanMode::Double;
        pos = advance(buffer, pos);
        continue;
      }
      if (c == '\'') {
        mode = ScanMode::Single;
        pos = advance(buffer, pos);
        continue;
      }
      if (c == '`') {
        mode = ScanMode::Backtick;
        pos = advance(buffer, pos);
        continue;
      }
      if (c == quote_ch) {
        open = pos;
        mode = ScanMode::Target;
        pos = advance(buffer, pos);
        continue;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::LineComment) {
      if (len == 0 || pos.col >= len - 1) {
        mode = ScanMode::Code;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::BlockComment) {
      if (c == '*' && next == '/') {
        mode = ScanMode::Code;
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::Double) {
      if (c == '"' && !is_escaped(buffer, pos)) {
        mode = ScanMode::Code;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::Single) {
      if (c == '\'' && !is_escaped(buffer, pos)) {
        mode = ScanMode::Code;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::Backtick) {
      if (c == '`' && !is_escaped(buffer, pos)) {
        mode = ScanMode::Code;
      }
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == ScanMode::Target) {
      if (c == quote_ch && !is_escaped(buffer, pos)) {
        if (open.line >= 0 && pos_before_or_at(open.line, open.col, line, col) &&
            pos_after_or_at(pos.line, pos.col, line, col)) {
          if (!best.valid || open.line > best.line_a ||
              (open.line == best.line_a && open.col > best.col_a)) {
            best.valid = true;
            best.line_a = open.line;
            best.col_a = open.col;
            best.line_b = pos.line;
            best.col_b = pos.col;
          }
        }
        mode = ScanMode::Code;
        open = {-1, -1};
      }
      pos = advance(buffer, pos);
      continue;
    }

    pos = advance(buffer, pos);
  }

  return best;
}

TextSpan find_enclosing_line_comment(const EditorBuffer& buffer, int line, int col) {
  TextSpan result;
  if (buffer.lines.empty() || line < 0 || line >= static_cast<int>(buffer.lines.size())) {
    return result;
  }

  LexMode mode = LexMode::Code;
  TextPos pos{line, 0};
  pos = normalize_forward(buffer, pos);
  TextPos comment_start{-1, -1};

  while (pos.line == line && pos.line >= 0) {
    const char c = char_at(buffer, pos);
    const char next = peek_next(buffer, pos);

    if (mode == LexMode::Code) {
      if (c == '/' && next == '/') {
        comment_start = pos;
        mode = LexMode::LineComment;
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      if (c == '/' && next == '*') {
        mode = LexMode::BlockComment;
        pos = skip_literal_forward(buffer, pos, &mode);
        pos = advance(buffer, pos);
        continue;
      }
      pos = skip_literal_forward(buffer, pos, &mode);
      pos = advance(buffer, pos);
      continue;
    }

    if (mode == LexMode::LineComment) {
      if (pos_before_or_at(comment_start.line, comment_start.col, line, col)) {
        result.valid = true;
        result.line_a = comment_start.line;
        result.col_a = comment_start.col;
        result.line_b = line;
        result.col_b = line_len(buffer, line);
      }
      return result;
    }

    pos = skip_literal_forward(buffer, pos, &mode);
    pos = advance(buffer, pos);
  }

  return result;
}

TextSpan find_enclosing_block_comment(const EditorBuffer& buffer, int line, int col) {
  TextSpan best;
  if (buffer.lines.empty()) {
    return best;
  }

  LexMode mode = LexMode::Code;
  TextPos pos{0, 0};
  pos = normalize_forward(buffer, pos);
  TextPos open{-1, -1};

  while (pos.line >= 0) {
    const char c = char_at(buffer, pos);
    const char next = peek_next(buffer, pos);

    if (mode == LexMode::Code) {
      if (c == '/' && next == '*') {
        open = pos;
        mode = LexMode::BlockComment;
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      pos = skip_literal_forward(buffer, pos, &mode);
      if (mode == LexMode::Code) {
        pos = advance(buffer, pos);
      }
      continue;
    }

    if (mode == LexMode::BlockComment) {
      if (c == '*' && next == '/') {
        const TextPos close = advance(buffer, pos);
        if (open.line >= 0 && pos_before_or_at(open.line, open.col, line, col) &&
            pos_after_or_at(close.line, close.col, line, col)) {
          if (!best.valid || open.line > best.line_a ||
              (open.line == best.line_a && open.col > best.col_a)) {
            best.valid = true;
            best.line_a = open.line;
            best.col_a = open.col;
            best.line_b = close.line;
            best.col_b = close.col + 1;
          }
        }
        mode = LexMode::Code;
        open = {-1, -1};
        pos = advance(buffer, advance(buffer, pos));
        continue;
      }
      pos = advance(buffer, pos);
      continue;
    }

    pos = skip_literal_forward(buffer, pos, &mode);
    if (mode == LexMode::Code) {
      pos = advance(buffer, pos);
    }
  }

  return best;
}

}  // namespace tgdb
