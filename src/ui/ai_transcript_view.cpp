#include "ui/ai_transcript_view.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"
#include "util/syntax_scope.hpp"

namespace tuide {
namespace {

using namespace ftxui;

bool starts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool is_ident_start(char c) {
  return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '~';
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

const std::unordered_set<std::string>& cpp_keywords() {
  static const std::unordered_set<std::string> k = {
      "alignas",     "alignof",   "and",        "and_eq",    "asm",       "auto",
      "bitand",      "bitor",     "bool",       "break",     "case",      "catch",
      "char",        "char8_t",   "char16_t",   "char32_t",  "class",     "compl",
      "concept",     "const",     "consteval",  "constexpr", "constinit", "const_cast",
      "continue",    "co_await",  "co_return",  "co_yield",  "decltype",  "default",
      "delete",      "do",        "double",     "dynamic_cast", "else",   "enum",
      "explicit",    "export",    "extern",     "false",     "float",     "for",
      "friend",      "goto",      "if",         "inline",    "int",       "long",
      "mutable",     "namespace", "new",        "noexcept",  "not",       "not_eq",
      "nullptr",     "operator",  "or",         "or_eq",     "private",   "protected",
      "public",      "register",  "reinterpret_cast", "requires", "return", "short",
      "signed",      "sizeof",    "static",     "static_assert", "static_cast", "struct",
      "switch",      "template",  "this",       "thread_local", "throw",  "true",
      "try",         "typedef",   "typeid",     "typename",  "union",     "unsigned",
      "using",       "virtual",   "void",       "volatile",  "wchar_t",   "while",
      "xor",         "xor_eq",    "override",   "final",     "size_t",    "ssize_t",
      "uint8_t",     "uint16_t",  "uint32_t",   "uint64_t",  "int8_t",    "int16_t",
      "int32_t",     "int64_t",   "ptrdiff_t",  "intptr_t",  "uintptr_t",
  };
  return k;
}

bool looks_like_code_snippet(std::string_view s) {
  std::string_view t = s;
  while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) {
    t.remove_prefix(1);
  }
  if (t.empty()) {
    return false;
  }
  if (starts_with(t, "//") || starts_with(t, "/*") || starts_with(t, "#include") ||
      starts_with(t, "#if") || starts_with(t, "#define") || starts_with(t, "│")) {
    return true;
  }
  if (starts_with(t, "fn ") || starts_with(t, "method ") || starts_with(t, "class ") ||
      starts_with(t, "struct ") || starts_with(t, "ns ") || starts_with(t, "var ")) {
    return true;
  }
  // MakeFoo(… / int foo(… — even if ')' was truncated from a long signature.
  if (t.find('(') != std::string_view::npos && is_ident_start(t.front())) {
    return true;
  }
  const bool has_paren =
      t.find('(') != std::string_view::npos && t.find(')') != std::string_view::npos;
  const bool has_scope = t.find("::") != std::string_view::npos;
  const bool has_arrow = t.find("->") != std::string_view::npos;
  const bool has_amp_type =
      t.find('&') != std::string_view::npos || t.find('*') != std::string_view::npos;
  if (has_paren && (has_scope || has_arrow || has_amp_type)) {
    return true;
  }
  static const char* kTypeish[] = {
      "int ",      "void ",   "bool ",   "auto ",     "const ",    "static ",  "constexpr ",
      "std::",     "unsigned ", "size_t ", "template<", "class ",   "struct ",  "namespace ",
      "enum ",     "using ",  "Element ", "Component ", "Color ",   "Decorator "};
  for (const char* p : kTypeish) {
    if (starts_with(t, p)) {
      return true;
    }
  }
  if (is_ident_start(t.front()) && std::isupper(static_cast<unsigned char>(t.front()))) {
    bool has_lower = false;
    for (char c : t) {
      if (std::islower(static_cast<unsigned char>(c))) {
        has_lower = true;
        break;
      }
    }
    if (has_lower && t.find(' ') == std::string_view::npos) {
      return true;
    }
  }
  if (t.find('_') != std::string_view::npos && t.size() >= 6 && is_ident_start(t.front()) &&
      t.find(' ') == std::string_view::npos) {
    return true;
  }
  return false;
}

bool is_system_line(std::string_view s) {
  return starts_with(s, "L0") || starts_with(s, "L1") || starts_with(s, "→ ") ||
         starts_with(s, "✓") || starts_with(s, "✗") || starts_with(s, "AI ") ||
         starts_with(s, "===") || starts_with(s, "cache:") || starts_with(s, "gguf:") ||
         starts_with(s, "llama-") || starts_with(s, "intent ") || starts_with(s, "index:") ||
         starts_with(s, "auto_download") || starts_with(s, "Comandos") || starts_with(s, "NL ") ||
         starts_with(s, "  /") || starts_with(s, "  $ ") || starts_with(s, "exit_code=") ||
         starts_with(s, "default:") || starts_with(s, "trace ") || starts_with(s, "Canales:") ||
         starts_with(s, "tokens léxicos:") || starts_with(s, "REPO_MAP") ||
         starts_with(s, "candidatos índice:");
}

bool is_result_header(std::string_view s) {
  return s.find("Resultados más probables") != std::string_view::npos ||
         s.find("Most likely results") != std::string_view::npos;
}

bool is_numbered_result(std::string_view s, std::size_t* num_end_out) {
  std::size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i >= s.size() || !std::isdigit(static_cast<unsigned char>(s[i]))) {
    return false;
  }
  while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
    ++i;
  }
  if (i + 1 >= s.size() || s[i] != '.' || !std::isspace(static_cast<unsigned char>(s[i + 1]))) {
    return false;
  }
  if (num_end_out != nullptr) {
    *num_end_out = i + 2;
  }
  return true;
}

Element highlight_code_snippet(std::string_view code) {
  Elements parts;
  std::size_t i = 0;
  auto push_raw = [&](std::size_t begin, std::size_t end, SyntaxScope scope) {
    if (begin >= end || begin >= code.size()) {
      return;
    }
    end = std::min(end, code.size());
    parts.push_back(text(std::string(code.substr(begin, end - begin))) |
                    DecoratorForSyntaxScope(scope));
  };

  while (i < code.size()) {
    const char c = code[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      const std::size_t begin = i;
      while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i]))) {
        ++i;
      }
      push_raw(begin, i, SyntaxScope::kDefault);
      continue;
    }
    if (c == '"' || c == '\'') {
      const char quote = c;
      const std::size_t begin = i++;
      while (i < code.size()) {
        if (code[i] == '\\' && i + 1 < code.size()) {
          i += 2;
          continue;
        }
        if (code[i] == quote) {
          ++i;
          break;
        }
        ++i;
      }
      push_raw(begin, i, SyntaxScope::kString);
      continue;
    }
    if (starts_with(code.substr(i), "//")) {
      push_raw(i, code.size(), SyntaxScope::kComment);
      break;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      const std::size_t begin = i++;
      while (i < code.size() &&
             (std::isalnum(static_cast<unsigned char>(code[i])) || code[i] == '.' || code[i] == '\'')) {
        ++i;
      }
      push_raw(begin, i, SyntaxScope::kNumber);
      continue;
    }
    if (is_ident_start(c)) {
      const std::size_t begin = i++;
      while (i < code.size() && is_ident_char(code[i])) {
        ++i;
      }
      const std::string word(code.substr(begin, i - begin));
      SyntaxScope scope = SyntaxScope::kVariable;
      if (cpp_keywords().count(word) > 0) {
        scope = SyntaxScope::kKeyword;
      } else {
        std::size_t j = i;
        while (j < code.size() && std::isspace(static_cast<unsigned char>(code[j]))) {
          ++j;
        }
        if (j < code.size() && code[j] == '(') {
          scope = SyntaxScope::kFunction;
        } else if (!word.empty() && std::isupper(static_cast<unsigned char>(word[0]))) {
          scope = SyntaxScope::kType;
        } else if (word.size() > 2 && word.compare(word.size() - 2, 2, "_t") == 0) {
          scope = SyntaxScope::kType;
        }
      }
      push_raw(begin, i, scope);
      continue;
    }
    // Operators / punctuation
    const std::size_t begin = i++;
    if ((c == ':' && i < code.size() && code[i] == ':') ||
        (c == '-' && i < code.size() && code[i] == '>') ||
        (c == '=' && i < code.size() && (code[i] == '=' || code[i] == '>')) ||
        (c == '!' && i < code.size() && code[i] == '=') ||
        (c == '<' && i < code.size() && (code[i] == '=' || code[i] == '<')) ||
        (c == '>' && i < code.size() && (code[i] == '=' || code[i] == '>'))) {
      ++i;
    }
    push_raw(begin, i, SyntaxScope::kOperator);
  }

  if (parts.empty()) {
    return text(code.empty() ? " " : std::string(code)) | color(theme::SyntaxDefault());
  }
  return hbox(std::move(parts));
}

Element with_indent(int level, Element inner) {
  if (level <= 0) {
    return inner;
  }
  return hbox({text(std::string(static_cast<std::size_t>(level) * 2, ' ')), std::move(inner)});
}

}  // namespace

std::optional<AiResultLocation> parse_ai_result_location(std::string_view line) {
  std::size_t num_end = 0;
  if (!is_numbered_result(line, &num_end)) {
    return std::nullopt;
  }
  std::string_view rest = line.substr(num_end);
  while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
    rest.remove_prefix(1);
  }
  while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) {
    rest.remove_suffix(1);
  }
  if (rest.empty()) {
    return std::nullopt;
  }

  AiResultLocation loc;
  const std::size_t last_colon = rest.rfind(':');
  if (last_colon != std::string_view::npos && last_colon + 1 < rest.size()) {
    bool all_digits = true;
    int value = 0;
    for (std::size_t i = last_colon + 1; i < rest.size(); ++i) {
      const unsigned char ch = static_cast<unsigned char>(rest[i]);
      if (!std::isdigit(ch)) {
        all_digits = false;
        break;
      }
      value = value * 10 + (rest[i] - '0');
    }
    if (all_digits && value > 0) {
      loc.line = value;
      rest = rest.substr(0, last_colon);
      while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) {
        rest.remove_suffix(1);
      }
    }
  }
  if (rest.empty()) {
    return std::nullopt;
  }
  if (rest.find('/') == std::string_view::npos && rest.find('\\') == std::string_view::npos &&
      rest.find('.') == std::string_view::npos) {
    return std::nullopt;
  }
  loc.path.assign(rest.begin(), rest.end());
  return loc;
}

Element render_ai_transcript_line(std::string_view line, bool hovered) {
  if (line.empty()) {
    return text(" ") | color(theme::Muted()) | bgcolor(theme::CodeBg());
  }

  if (starts_with(line, "> ")) {
    return hbox({text("❯ ") | color(theme::Accent()) | bold,
                 text(std::string(line.substr(2))) | color(theme::TitleText())}) |
           bgcolor(theme::CodeBg());
  }

  if (starts_with(line, "L1 needles propuestos")) {
    return text(std::string(line)) | color(theme::Accent()) | bold | bgcolor(theme::CodeBg());
  }
  if (starts_with(line, "  • ") || starts_with(line, "  - ")) {
    return text(std::string(line)) | color(theme::TitleText()) | bgcolor(theme::CodeBg());
  }
  if (starts_with(line, "tokens léxicos:")) {
    return text(std::string(line)) | color(theme::Muted()) | bgcolor(theme::CodeBg());
  }

  if (is_system_line(line)) {
    return text(std::string(line)) | color(theme::Muted()) | bgcolor(theme::CodeBg());
  }

  if (is_result_header(line)) {
    return text(std::string(line)) | color(theme::Accent()) | bold | bgcolor(theme::CodeBg());
  }

  std::size_t num_end = 0;
  if (is_numbered_result(line, &num_end)) {
    const std::string_view prefix = line.substr(0, num_end);
    const std::string_view rest = line.substr(num_end);
    // Path:line — accent number, file path in FileText (not flat Header).
    Element row =
        with_indent(1, hbox({text(std::string(prefix)) | color(theme::Accent()) | bold,
                             text(std::string(rest)) | color(theme::FileText())}));
    if (hovered) {
      return row | bold | bgcolor(theme::TabHover());
    }
    return row | bgcolor(theme::CodeBg());
  }

  // Preserve existing leading spaces as base indent, then treat as code or reply.
  std::size_t lead = 0;
  while (lead < line.size() && line[lead] == ' ') {
    ++lead;
  }
  const std::string_view body = line.substr(lead);
  const int indent_from_spaces = static_cast<int>(lead / 2);
  const int reply_indent = std::max(1, indent_from_spaces);

  // Investigate signatures are emitted with 4 leading spaces — always highlight.
  if (lead >= 4 || looks_like_code_snippet(body)) {
    return with_indent(std::max(2, reply_indent), highlight_code_snippet(body)) |
           bgcolor(theme::CodeBg());
  }

  // Plain reply body: one level under prompts/headers.
  return with_indent(reply_indent, text(std::string(body)) | color(theme::Header())) |
         bgcolor(theme::CodeBg());
}

}  // namespace tuide
