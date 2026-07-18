#include "util/syntax_scope.hpp"

#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

namespace {

SyntaxScope SyntaxScopeForTreeSitterCaptureImpl(const std::string& capture) {
  // Dotted captures (e.g. function.method, constant.builtin) map by prefix.
  std::string base = capture;
  const auto dot = base.find('.');
  if (dot != std::string::npos) {
    base = base.substr(0, dot);
  }
  if (base == "comment") {
    return SyntaxScope::kComment;
  }
  if (base == "string" || base == "escape") {
    return SyntaxScope::kString;
  }
  if (base == "number") {
    return SyntaxScope::kNumber;
  }
  if (base == "keyword" || base == "operator") {
    return SyntaxScope::kKeyword;
  }
  if (base == "macro") {
    return SyntaxScope::kMacro;
  }
  if (base == "namespace") {
    return SyntaxScope::kNamespace;
  }
  if (base == "type" || base == "constructor") {
    return SyntaxScope::kType;
  }
  if (base == "function") {
    return SyntaxScope::kFunction;
  }
  if (base == "parameter") {
    return SyntaxScope::kParameter;
  }
  if (base == "property") {
    return SyntaxScope::kProperty;
  }
  // `nullptr` / `this`-like builtins read as keywords in both highlighters.
  if (base == "constant" || capture == "variable.builtin" || capture == "constant.builtin") {
    return SyntaxScope::kKeyword;
  }
  if (base == "variable") {
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

}  // namespace tuide
