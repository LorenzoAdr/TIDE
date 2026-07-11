#include "util/syntax_scope.hpp"

#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

SyntaxScope SyntaxScopeForTreeSitterCaptureImpl(const std::string& capture) {
  if (capture == "comment") {
    return SyntaxScope::kComment;
  }
  if (capture == "string") {
    return SyntaxScope::kString;
  }
  if (capture == "number") {
    return SyntaxScope::kNumber;
  }
  if (capture == "keyword") {
    return SyntaxScope::kKeyword;
  }
  if (capture == "macro") {
    return SyntaxScope::kMacro;
  }
  if (capture == "namespace") {
    return SyntaxScope::kNamespace;
  }
  if (capture == "type") {
    return SyntaxScope::kType;
  }
  if (capture == "function") {
    return SyntaxScope::kFunction;
  }
  if (capture == "parameter") {
    return SyntaxScope::kParameter;
  }
  if (capture == "property") {
    return SyntaxScope::kProperty;
  }
  // `nullptr` / `this`-like builtins read as keywords in both highlighters.
  if (capture == "constant" || capture == "variable.builtin") {
    return SyntaxScope::kKeyword;
  }
  if (capture == "variable") {
    return SyntaxScope::kVariable;
  }
  return SyntaxScope::kDefault;
}

SyntaxScope SyntaxScopeForLspTokenTypeImpl(const std::string& token_type) {
  if (token_type == "comment") {
    return SyntaxScope::kComment;
  }
  if (token_type == "string" || token_type == "regexp") {
    return SyntaxScope::kString;
  }
  if (token_type == "number") {
    return SyntaxScope::kNumber;
  }
  if (token_type == "keyword" || token_type == "modifier") {
    return SyntaxScope::kKeyword;
  }
  if (token_type == "macro" || token_type == "decorator") {
    return SyntaxScope::kMacro;
  }
  if (token_type == "namespace") {
    return SyntaxScope::kNamespace;
  }
  if (token_type == "type" || token_type == "class" || token_type == "struct" ||
      token_type == "enum" || token_type == "interface" || token_type == "typeParameter") {
    return SyntaxScope::kType;
  }
  if (token_type == "function" || token_type == "method" || token_type == "event") {
    return SyntaxScope::kFunction;
  }
  if (token_type == "parameter") {
    return SyntaxScope::kParameter;
  }
  if (token_type == "property" || token_type == "enumMember") {
    return SyntaxScope::kProperty;
  }
  if (token_type == "operator") {
    return SyntaxScope::kOperator;
  }
  if (token_type == "variable") {
    return SyntaxScope::kVariable;
  }
  return SyntaxScope::kDefault;
}

}  // namespace

SyntaxScope SyntaxScopeForTreeSitterCapture(const std::string& capture) {
  return SyntaxScopeForTreeSitterCaptureImpl(capture);
}

SyntaxScope SyntaxScopeForLspTokenType(const std::string& token_type) {
  return SyntaxScopeForLspTokenTypeImpl(token_type);
}

Decorator DecoratorForSyntaxScope(SyntaxScope scope) {
  switch (scope) {
    case SyntaxScope::kComment:
      return color(theme::SyntaxComment()) | dim;
    case SyntaxScope::kString:
      return color(theme::SyntaxString());
    case SyntaxScope::kNumber:
      return color(theme::SyntaxNumber());
    case SyntaxScope::kKeyword:
      return color(theme::SyntaxKeyword()) | bold;
    case SyntaxScope::kMacro:
      return color(theme::SyntaxMacro());
    case SyntaxScope::kNamespace:
      return color(theme::SyntaxNamespace());
    case SyntaxScope::kType:
      return color(theme::SyntaxType());
    case SyntaxScope::kFunction:
      return color(theme::SyntaxFunction());
    case SyntaxScope::kParameter:
      return color(theme::SyntaxParameter());
    case SyntaxScope::kProperty:
      return color(theme::SyntaxProperty());
    case SyntaxScope::kOperator:
      return color(theme::SyntaxOperator());
    case SyntaxScope::kVariable:
      return color(theme::SyntaxVariable());
    case SyntaxScope::kDefault:
      return color(theme::SyntaxDefault());
  }
  return color(theme::SyntaxDefault());
}

bool SyntaxScopeUsesInvertedCursor(SyntaxScope scope) {
  return scope == SyntaxScope::kKeyword || scope == SyntaxScope::kType;
}

}  // namespace tgdb
