#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_language.hpp"
#include "util/syntax_scope.hpp"

namespace tuide {

TreeSitterLangKind tree_sitter_lang_kind_for_alias(const std::string&) {
  return TreeSitterLangKind::kNone;
}

const TSLanguage* tree_sitter_language_for_kind(TreeSitterLangKind) {
  return nullptr;
}

std::vector<LineHighlights> highlights_for_document(
    TSNode, const std::string&, TreeSitterLangKind) {
  return {};
}

SyntaxScope SyntaxScopeForTreeSitterCapture(const std::string&) {
  return SyntaxScope::kDefault;
}

}  // namespace tuide
