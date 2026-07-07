#include "parser/tree_sitter_blocks.hpp"

#include <cstring>
#include <algorithm>
#include <stack>
#include <vector>

#include "parser/tree_sitter_ast_utils.hpp"
#include "parser/tree_sitter_document.hpp"

namespace tgdb {

namespace {

bool is_bracket_char(char c) { return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}'; }

bool is_opening(char c) { return c == '(' || c == '[' || c == '{'; }

bool is_closing(char c) { return c == ')' || c == ']' || c == '}'; }

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

bool is_non_code_node(TSNode node) {
  while (!ts_node_is_null(node)) {
    const char* type = ts_node_type(node);
    if (type == nullptr) {
      break;
    }
    if (std::strcmp(type, "comment") == 0 || std::strcmp(type, "string_literal") == 0 ||
        std::strcmp(type, "raw_string_literal") == 0 || std::strcmp(type, "char_literal") == 0) {
      return true;
    }
    node = ts_node_parent(node);
  }
  return false;
}

TSNode node_at_point(TSNode root, int line, int col) {
  const TSPoint start = make_ts_point(line, col);
  const TSPoint end = make_ts_point(line, col + 1);
  return ts_node_descendant_for_point_range(root, start, end);
}

struct BracketPos {
  int line = 0;
  int col = 0;
  char ch = '\0';
};

void collect_brackets(TSNode node, std::vector<BracketPos>* out) {
  if (ts_node_is_null(node) || out == nullptr) {
    return;
  }
  const char* type = ts_node_type(node);
  if (type != nullptr && std::strlen(type) == 1 && is_bracket_char(type[0]) && !is_non_code_node(node)) {
    const TSPoint start = ts_node_start_point(node);
    out->push_back({static_cast<int>(start.row), static_cast<int>(start.column), type[0]});
  }
  const uint32_t count = ts_node_child_count(node);
  for (uint32_t i = 0; i < count; ++i) {
    collect_brackets(ts_node_child(node, i), out);
  }
}

BracketPairHighlight match_bracket_at(const std::vector<BracketPos>& brackets, int line, int col) {
  BracketPairHighlight result;
  const char ch = [&]() {
    for (const BracketPos& bracket : brackets) {
      if (bracket.line == line && bracket.col == col) {
        return bracket.ch;
      }
    }
    return '\0';
  }();
  if (!is_opening(ch) && !is_closing(ch)) {
    return result;
  }

  const bool from_close = is_closing(ch);
  const char open = from_close ? opening_for(ch) : ch;
  const char close = closing_for(open);
  if (open == '\0') {
    return result;
  }

  std::stack<BracketPos> stack;
  for (const BracketPos& pos : brackets) {
    if (pos.ch == open) {
      stack.push(pos);
    } else if (pos.ch == close) {
      if (stack.empty()) {
        if (from_close && pos.line == line && pos.col == col) {
          return result;
        }
      } else {
        const BracketPos open_pos = stack.top();
        stack.pop();
        if ((from_close && pos.line == line && pos.col == col) ||
            (!from_close && open_pos.line == line && open_pos.col == col)) {
          result.valid = true;
          result.line_a = open_pos.line;
          result.col_a = open_pos.col;
          result.line_b = pos.line;
          result.col_b = pos.col;
          return result;
        }
      }
    }
  }
  return result;
}

struct Pos {
  int line = 0;
  int col = 0;
};

Pos retreat(const std::string& source, Pos pos) {
  if (pos.col > 0) {
    return {pos.line, pos.col - 1};
  }
  if (pos.line <= 0) {
    return {-1, -1};
  }
  const std::size_t begin = [&]() {
    std::size_t offset = 0;
    for (int row = 0; row < pos.line; ++row) {
      const std::size_t next = source.find('\n', offset);
      if (next == std::string::npos) {
        return source.size();
      }
      offset = next + 1;
    }
    return offset;
  }();
  const std::size_t end = source.find('\n', begin);
  const int len =
      static_cast<int>((end == std::string::npos ? source.size() : end) - begin);
  return {pos.line - 1, len - 1};
}

char char_at_source(const std::string& source, int line, int col) {
  std::size_t offset = 0;
  for (int row = 0; row < line; ++row) {
    const std::size_t next = source.find('\n', offset);
    if (next == std::string::npos) {
      return '\0';
    }
    offset = next + 1;
  }
  const std::size_t index = offset + static_cast<std::size_t>(col);
  if (index >= source.size()) {
    return '\0';
  }
  return source[index];
}

TSNode enclosing_literal_node(TSNode root, int line, int col) {
  TSNode node = node_at_point(root, line, col);
  while (!ts_node_is_null(node)) {
    const char* type = ts_node_type(node);
    if (type != nullptr &&
        (std::strcmp(type, "string_literal") == 0 || std::strcmp(type, "raw_string_literal") == 0 ||
         std::strcmp(type, "char_literal") == 0)) {
      return node;
    }
    node = ts_node_parent(node);
  }
  return TSNode{};
}

TSNode enclosing_comment_node(TSNode root, int line, int col) {
  TSNode node = node_at_point(root, line, col);
  while (!ts_node_is_null(node)) {
    const char* type = ts_node_type(node);
    if (type != nullptr && std::strcmp(type, "comment") == 0) {
      return node;
    }
    node = ts_node_parent(node);
  }
  return TSNode{};
}

TextSpan span_for_node(TSNode node) {
  TextSpan span;
  if (ts_node_is_null(node)) {
    return span;
  }
  const TSPoint start = ts_node_start_point(node);
  const TSPoint end = ts_node_end_point(node);
  span.valid = true;
  span.line_a = static_cast<int>(start.row);
  span.col_a = static_cast<int>(start.column);
  span.line_b = static_cast<int>(end.row);
  span.col_b = static_cast<int>(end.column);
  return span;
}

}  // namespace

bool cursor_in_code_node(TSNode root, const std::string& source, int line, int col) {
  (void)source;
  if (ts_node_is_null(root)) {
    return false;
  }
  TSNode node = node_at_point(root, line, col);
  return !is_non_code_node(node);
}

BracketPairHighlight bracket_pair_at(TSNode root, const std::string& source, int line, int col) {
  (void)source;
  if (ts_node_is_null(root) || !cursor_in_code_node(root, source, line, col)) {
    return {};
  }
  std::vector<BracketPos> brackets;
  collect_brackets(root, &brackets);
  return match_bracket_at(brackets, line, col);
}

BracketPairHighlight enclosing_bracket_pair(TSNode root, const std::string& source, int line,
                                            int col, char open_ch) {
  BracketPairHighlight current = bracket_pair_at(root, source, line, col);
  if (current.valid) {
    return current;
  }
  if (ts_node_is_null(root)) {
    return {};
  }
  std::vector<BracketPos> brackets;
  collect_brackets(root, &brackets);
  Pos pos{line, col};
  while (pos.line >= 0) {
    if (cursor_in_code_node(root, source, pos.line, pos.col)) {
      const char c = char_at_source(source, pos.line, pos.col);
      if (c == open_ch || c == closing_for(open_ch)) {
        BracketPairHighlight found = match_bracket_at(brackets, pos.line, pos.col);
        if (found.valid) {
          return found;
        }
      }
    }
    pos = retreat(source, pos);
  }
  return {};
}

BracketPairHighlight enclosing_bracket_pair_for_block_nav(TSNode root, const std::string& source,
                                                          int line, int col, char open_ch,
                                                          bool jump_to_start) {
  BracketPairHighlight inner = enclosing_bracket_pair(root, source, line, col, open_ch);
  if (!inner.valid) {
    return inner;
  }
  if (!jump_to_start) {
    return inner;
  }
  const char close_ch = closing_for(open_ch);
  if (line == inner.line_b && col == inner.col_b) {
    Pos outer_pos = {inner.line_a, inner.col_a};
    outer_pos = retreat(source, outer_pos);
    while (outer_pos.line >= 0) {
      BracketPairHighlight outer =
          enclosing_bracket_pair(root, source, outer_pos.line, outer_pos.col, open_ch);
      if (outer.valid &&
          (outer.line_a != inner.line_a || outer.col_a != inner.col_a ||
           outer.line_b != inner.line_b || outer.col_b != inner.col_b)) {
        return outer;
      }
      outer_pos = retreat(source, outer_pos);
    }
  }
  if (line == inner.line_a && col == inner.col_a) {
    const char ch = char_at_source(source, line, col);
    if (ch == close_ch) {
      Pos outer_pos = retreat(source, {line, col});
      while (outer_pos.line >= 0) {
        BracketPairHighlight outer =
            enclosing_bracket_pair(root, source, outer_pos.line, outer_pos.col, open_ch);
        if (outer.valid) {
          return outer;
        }
        outer_pos = retreat(source, outer_pos);
      }
    }
  }
  return inner;
}

TextSpan enclosing_quote_pair(TSNode root, const std::string& source, int line, int col,
                              char quote_ch) {
  (void)source;
  (void)quote_ch;
  if (ts_node_is_null(root)) {
    return {};
  }
  return span_for_node(enclosing_literal_node(root, line, col));
}

TextSpan enclosing_line_comment(TSNode root, const std::string& source, int line, int col) {
  (void)source;
  if (ts_node_is_null(root)) {
    return {};
  }
  TSNode comment = enclosing_comment_node(root, line, col);
  if (ts_node_is_null(comment)) {
    return {};
  }
  const std::string text = ts_node_text(comment, source);
  if (text.size() < 2 || text[0] != '/' || text[1] != '/') {
    return {};
  }
  if (static_cast<int>(ts_node_start_point(comment).row) != line) {
    return {};
  }
  if (col < static_cast<int>(ts_node_start_point(comment).column)) {
    return {};
  }
  return span_for_node(comment);
}

TextSpan enclosing_block_comment(TSNode root, const std::string& source, int line, int col) {
  if (ts_node_is_null(root)) {
    return {};
  }
  TSNode comment = enclosing_comment_node(root, line, col);
  if (ts_node_is_null(comment)) {
    return {};
  }
  const std::string text = ts_node_text(comment, source);
  if (text.size() < 2 || text[0] != '/' || text[1] != '*') {
    return {};
  }
  return span_for_node(comment);
}

std::vector<ColoredBraceMarker> colored_curly_braces(TSNode root, const std::string& source) {
  (void)source;
  std::vector<ColoredBraceMarker> markers;
  if (ts_node_is_null(root)) {
    return markers;
  }

  std::vector<BracketPos> brackets;
  collect_brackets(root, &brackets);
  std::vector<BracketPos> curlies;
  curlies.reserve(brackets.size());
  for (const BracketPos& pos : brackets) {
    if (pos.ch == '{' || pos.ch == '}') {
      curlies.push_back(pos);
    }
  }
  std::sort(curlies.begin(), curlies.end(), [](const BracketPos& a, const BracketPos& b) {
    if (a.line != b.line) {
      return a.line < b.line;
    }
    return a.col < b.col;
  });

  std::stack<int> open_depths;
  markers.reserve(curlies.size());
  for (const BracketPos& pos : curlies) {
    if (pos.ch == '{') {
      const int depth = static_cast<int>(open_depths.size());
      open_depths.push(depth);
      markers.push_back({pos.line, pos.col, depth});
    } else if (pos.ch == '}' && !open_depths.empty()) {
      const int depth = open_depths.top();
      open_depths.pop();
      markers.push_back({pos.line, pos.col, depth});
    }
  }
  return markers;
}

}  // namespace tgdb
