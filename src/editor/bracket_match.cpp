#include "editor/bracket_match.hpp"

#include "editor/editor_buffer_source.hpp"
#include "editor/editor_state.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_service.hpp"

namespace tuide {

BracketPairHighlight find_bracket_pair_highlight(const EditorBuffer& buffer, int line, int col) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().bracket_pair_highlight(buffer.path, source, line, col);
}

BracketPairHighlight find_scope_bracket_pair(const EditorBuffer& buffer, int line, int col) {
  return tree_sitter_service().scope_bracket_pair_at(buffer.path, buffer.lines.to_vector(), line, col);
}

std::vector<ColoredBraceMarker> find_colored_curly_braces(const EditorBuffer& buffer) {
  return tree_sitter_service().colored_curly_braces_at(buffer.path, buffer.lines.to_vector());
}

BracketPairHighlight find_enclosing_bracket_pair(const EditorBuffer& buffer, int line, int col,
                                                 char open_ch) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().enclosing_bracket_pair(buffer.path, source, line, col, open_ch);
}

BracketPairHighlight find_innermost_enclosing_pair(const EditorBuffer& buffer, int line, int col) {
  for (const char open_ch : {'{', '[', '('}) {
    BracketPairHighlight pair = find_enclosing_bracket_pair(buffer, line, col, open_ch);
    if (pair.valid) {
      return pair;
    }
  }
  return {};
}

BracketPairHighlight find_enclosing_bracket_pair_for_block_nav(const EditorBuffer& buffer,
                                                               int line, int col, char open_ch,
                                                               bool jump_to_start) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().enclosing_bracket_pair_for_block_nav(buffer.path, source, line, col,
                                                                    open_ch, jump_to_start);
}

TextSpan find_enclosing_quote_pair(const EditorBuffer& buffer, int line, int col, char quote_ch) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().enclosing_quote_pair(buffer.path, source, line, col, quote_ch);
}

TextSpan find_enclosing_line_comment(const EditorBuffer& buffer, int line, int col) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().enclosing_line_comment(buffer.path, source, line, col);
}

TextSpan find_enclosing_block_comment(const EditorBuffer& buffer, int line, int col) {
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().enclosing_block_comment(buffer.path, source, line, col);
}

bool cursor_in_code(const EditorBuffer& buffer, int line, int col) {
  // Hot path: called on every keystroke (auto-pair detection in
  // insert_char_at_with_pairs). Reuse the incrementally-maintained joined
  // source cache instead of re-joining the whole document from scratch on
  // every single character -- that used to be an O(document size) cost paid
  // per keystroke, regardless of the vector<string>/rope backend underneath.
  const std::string& source = editor_buffer_joined_source(buffer);
  return tree_sitter_service().cursor_in_code(buffer.path, source, line, col);
}

}  // namespace tuide
