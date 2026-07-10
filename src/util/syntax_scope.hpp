#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

// Canonical highlighting scope. The tree-sitter highlighter and the LSP
// semantic-tokens highlighter each speak their own vocabulary of capture /
// token-type names; both are translated into one of these scopes before a
// color is picked, so the same piece of code always resolves to the same
// color regardless of which of the two highlighting phases produced it.
// This is what prevents the tree-sitter -> LSP handoff from visibly
// "flickering" between different colors for the same token.
enum class SyntaxScope {
  kDefault,
  kComment,
  kString,
  kNumber,
  kKeyword,
  kMacro,
  kNamespace,
  kType,
  kFunction,
  kParameter,
  kProperty,
  kVariable,
  kOperator,
};

// Translates a tree-sitter query capture name (e.g. "type", "variable.builtin")
// into its canonical scope.
SyntaxScope SyntaxScopeForTreeSitterCapture(const std::string& capture);

// Translates an LSP semantic token type name (e.g. "class", "typeParameter",
// as negotiated via the server's semantic tokens legend) into its canonical
// scope.
SyntaxScope SyntaxScopeForLspTokenType(const std::string& token_type);

// The single color/style mapping used by every highlighting phase for a given
// canonical scope.
ftxui::Decorator DecoratorForSyntaxScope(SyntaxScope scope);

// Scopes that read best with a block/inverted cursor cell instead of the
// normal cursor decorator.
bool SyntaxScopeUsesInvertedCursor(SyntaxScope scope);

}  // namespace tgdb
