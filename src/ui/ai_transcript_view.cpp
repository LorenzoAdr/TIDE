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
  // Prefijos narrativos / detalle: nunca sintaxis rainbow.
  if (starts_with(t, "→ ") || starts_with(t, "· ") || starts_with(t, "• ")) {
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
  // Firmas con paréntesis: exige aspecto de código (scope/tipo), no "buscar Foo(".
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
  // Ident CamelCase suelto / snake_case: demasiado agresivo para notebook/paths — no.
  return false;
}

bool is_meta_noise_line(std::string_view s) {
  // Ruido técnico / L0 / cache — no narrativa del piloto.
  return starts_with(s, "L0") || starts_with(s, "L1") || starts_with(s, "✓") ||
         starts_with(s, "✗") || starts_with(s, "AI ") || starts_with(s, "===") ||
         starts_with(s, "cache:") || starts_with(s, "gguf:") || starts_with(s, "llama-") ||
         starts_with(s, "intent ") || starts_with(s, "index:") || starts_with(s, "auto_download") ||
         starts_with(s, "Comandos") || starts_with(s, "NL ") || starts_with(s, "  /") ||
         starts_with(s, "  $ ") || starts_with(s, "exit_code=") || starts_with(s, "default:") ||
         starts_with(s, "trace ") || starts_with(s, "Canales:") ||
         starts_with(s, "tokens léxicos:") || starts_with(s, "REPO_MAP") ||
         starts_with(s, "candidatos índice:");
}

bool is_pilot_narrative_line(std::string_view s) {
  // Narrativa del admin/piloto (órdenes, veredictos, cierre).
  if (!starts_with(s, "→ ")) {
    return false;
  }
  return starts_with(s, "→ Piloto:") || starts_with(s, "→ Explorador:") ||
         starts_with(s, "→ Siguiendo") || starts_with(s, "→ Veredicto:") ||
         starts_with(s, "→ Respuesta:") || starts_with(s, "→ Necesito") ||
         starts_with(s, "→ Investigando") || starts_with(s, "→ Sigo con") ||
         starts_with(s, "→ Proponiendo") || starts_with(s, "→ Edición") ||
         starts_with(s, "→ Explore agotado") || starts_with(s, "→ Sin más exploradores") ||
         starts_with(s, "→ Verificando") || starts_with(s, "→ Tope de");
}

bool is_pilot_meta_line(std::string_view s) {
  // Timing / rechazos / errores — visibles pero secundarios al piloto.
  return starts_with(s, "→ Modelo respondió") || starts_with(s, "→ Error del modelo") ||
         starts_with(s, "→ No se pudo aplicar") ||
         (starts_with(s, "→ ") && !is_pilot_narrative_line(s));
}

bool is_explorer_detail_line(std::string_view s) {
  // Detalle interno del explorador / verificador (grep, leer, hints).
  return starts_with(s, "  · ") || starts_with(s, "  - ") || starts_with(s, "  • ");
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

// Markdown inline ligero: produce spans de CONTENIDO (los marcadores son huecos).
// Así un soft-wrap que parte "**hola**" no pinta los asteriscos: nunca entran en un span.
enum class MdKind { Plain, Bold, Code, Emph };

struct MdSpan {
  std::size_t begin = 0;  // inclusive, en el string original
  std::size_t end = 0;    // exclusive
  MdKind kind = MdKind::Plain;
};

std::vector<MdSpan> parse_markdown_spans(std::string_view body) {
  std::vector<MdSpan> spans;
  auto emit = [&](std::size_t b, std::size_t e, MdKind k) {
    if (b >= e || b >= body.size()) {
      return;
    }
    e = std::min(e, body.size());
    if (!spans.empty() && spans.back().kind == k && spans.back().end == b) {
      spans.back().end = e;
      return;
    }
    spans.push_back(MdSpan{b, e, k});
  };

  std::size_t i = 0;
  std::size_t plain_b = 0;
  auto flush_plain = [&](std::size_t upto) {
    if (plain_b < upto) {
      emit(plain_b, upto, MdKind::Plain);
    }
    plain_b = upto;
  };

  while (i < body.size()) {
    if (i + 1 < body.size() && body[i] == '*' && body[i + 1] == '*') {
      const auto close = body.find("**", i + 2);
      if (close != std::string_view::npos) {
        flush_plain(i);
        emit(i + 2, close, MdKind::Bold);
        i = close + 2;
        plain_b = i;
        continue;
      }
    }
    if (i + 1 < body.size() && body[i] == '_' && body[i + 1] == '_') {
      const auto close = body.find("__", i + 2);
      if (close != std::string_view::npos) {
        flush_plain(i);
        emit(i + 2, close, MdKind::Bold);
        i = close + 2;
        plain_b = i;
        continue;
      }
    }
    if (body[i] == '`') {
      const auto close = body.find('`', i + 1);
      if (close != std::string_view::npos) {
        flush_plain(i);
        emit(i + 1, close, MdKind::Code);
        i = close + 1;
        plain_b = i;
        continue;
      }
    }
    if (body[i] == '*' && (i + 1 >= body.size() || body[i + 1] != '*')) {
      const auto close = body.find('*', i + 1);
      if (close != std::string_view::npos && close > i + 1) {
        const std::string_view inner = body.substr(i + 1, close - (i + 1));
        if (inner.find(' ') == std::string_view::npos &&
            inner.find('*') == std::string_view::npos) {
          flush_plain(i);
          emit(i + 1, close, MdKind::Emph);
          i = close + 1;
          plain_b = i;
          continue;
        }
      }
    }
    ++i;
  }
  flush_plain(body.size());
  return spans;
}

Decorator style_for_md(MdKind kind, Color base) {
  switch (kind) {
    case MdKind::Bold:
      return color(base) | bold;
    case MdKind::Code:
      return color(theme::Accent()) | bgcolor(theme::CodeBg());
    case MdKind::Emph:
      return color(base) | dim;
    case MdKind::Plain:
    default:
      return color(base);
  }
}

Element render_markdown_slice(std::string_view body, std::size_t slice_begin, std::size_t slice_end,
                              Color base, Decorator extra = nothing) {
  slice_end = std::min(slice_end, body.size());
  slice_begin = std::min(slice_begin, slice_end);
  if (slice_begin >= slice_end) {
    return text(" ") | color(base) | extra;
  }

  const auto spans = parse_markdown_spans(body);
  Elements parts;
  for (const auto& sp : spans) {
    const std::size_t lo = std::max(sp.begin, slice_begin);
    const std::size_t hi = std::min(sp.end, slice_end);
    if (lo >= hi) {
      continue;
    }
    parts.push_back(text(std::string(body.substr(lo, hi - lo))) | style_for_md(sp.kind, base) |
                    extra);
  }
  // Si el slice cayó solo sobre marcadores, no dejes la fila vacía.
  if (parts.empty()) {
    return text(" ") | color(base) | extra;
  }
  if (parts.size() == 1) {
    return std::move(parts.front());
  }
  return hbox(std::move(parts));
}

Element render_markdown_inline(std::string_view body, Color base) {
  return render_markdown_slice(body, 0, body.size(), base);
}

Element render_reply_body_slice(std::string_view full, std::size_t slice_begin,
                                std::size_t slice_end, bool first_visual_segment) {
  slice_end = std::min(slice_end, full.size());
  slice_begin = std::min(slice_begin, slice_end);

  std::size_t lead = 0;
  while (lead < full.size() && full[lead] == ' ') {
    ++lead;
  }

  if (first_visual_segment && slice_begin <= lead && lead < full.size()) {
    const std::string_view at = full.substr(lead);
    std::size_t prefix = 0;
    int heading = 0;
    if (starts_with(at, "### ")) {
      heading = 3;
      prefix = 4;
    } else if (starts_with(at, "## ")) {
      heading = 2;
      prefix = 3;
    } else if (starts_with(at, "# ")) {
      heading = 1;
      prefix = 2;
    }
    if (heading > 0) {
      const std::size_t content_begin = lead + prefix;
      return render_markdown_slice(full, std::max(content_begin, slice_begin), slice_end,
                                   theme::Accent(), bold);
    }
    if ((starts_with(at, "- ") || starts_with(at, "* ")) && !starts_with(at, "**")) {
      Elements row;
      if (slice_begin <= lead && slice_end > lead) {
        row.push_back(text("· ") | color(theme::Muted()));
      }
      row.push_back(render_markdown_slice(full, std::max(lead + 2, slice_begin), slice_end,
                                          theme::TitleText()));
      if (row.size() == 1) {
        return std::move(row.front());
      }
      return hbox(std::move(row));
    }
  }

  // El indent visual ya aporta el margen; no repintas espacios leading.
  const std::size_t lo = std::max(slice_begin, lead);
  return render_markdown_slice(full, lo, slice_end, theme::TitleText());
}

}  // namespace

std::string ai_markdown_plain(std::string_view line) {
  const auto spans = parse_markdown_spans(line);
  std::string out;
  out.reserve(line.size());
  for (const auto& sp : spans) {
    out.append(line.substr(sp.begin, sp.end - sp.begin));
  }
  return out;
}

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

Element render_ai_transcript_line(std::string_view line, bool hovered, std::size_t slice_begin,
                                  std::size_t slice_end) {
  if (slice_end == std::string_view::npos || slice_end > line.size()) {
    slice_end = line.size();
  }
  if (slice_begin > slice_end) {
    slice_begin = slice_end;
  }

  if (line.empty() || slice_begin >= slice_end) {
    return text(" ") | color(theme::Muted()) | bgcolor(theme::CodeBg());
  }

  // Continuaciones de wrap: markdown sobre la línea completa; hereda estilo del prefijo.
  if (slice_begin > 0) {
    Color base = theme::TitleText();
    Decorator extra = nothing;
    if (is_pilot_narrative_line(line)) {
      base = theme::Accent();
      extra = bold;
    } else if (is_explorer_detail_line(line)) {
      base = theme::Muted();
    } else if (is_pilot_meta_line(line)) {
      base = theme::Header();
    }
    std::size_t lead = slice_begin;
    while (lead < slice_end && line[lead] == ' ') {
      ++lead;
    }
    if (lead >= slice_end) {
      return text(" ") | color(theme::Muted()) | bgcolor(theme::CodeBg());
    }
    return with_indent(1, render_markdown_slice(line, lead, slice_end, base, extra)) |
           bgcolor(theme::CodeBg());
  }

  // slice_begin == 0: clasifica con el prefijo de la línea completa.
  if (starts_with(line, "> ")) {
    const std::size_t content = 2;
    Elements row = {text("❯ ") | color(theme::Accent()) | bold};
    if (slice_end > content) {
      row.push_back(render_markdown_slice(line, std::max(content, slice_begin), slice_end,
                                          theme::TitleText()));
    }
    return hbox(std::move(row)) | bgcolor(theme::CodeBg());
  }

  if (starts_with(line, "L1 needles propuestos")) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Accent(), bold) |
           bgcolor(theme::CodeBg());
  }
  if (starts_with(line, "tokens léxicos:")) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Muted()) |
           bgcolor(theme::CodeBg());
  }

  // 1) Narrativa del piloto — markdown inline (p.ej. veredicto con **…**).
  if (is_pilot_narrative_line(line)) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Accent(), bold) |
           bgcolor(theme::CodeBg());
  }

  // 2) Detalle explorador/verificador.
  if (is_explorer_detail_line(line)) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Muted()) |
           bgcolor(theme::CodeBg());
  }

  // 3) Meta piloto.
  if (is_pilot_meta_line(line)) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Header()) |
           bgcolor(theme::CodeBg());
  }

  if (is_meta_noise_line(line)) {
    return text(std::string(line.substr(slice_begin, slice_end - slice_begin))) |
           color(theme::Muted()) | bgcolor(theme::CodeBg());
  }

  if (is_result_header(line)) {
    return render_markdown_slice(line, slice_begin, slice_end, theme::Accent(), bold) |
           bgcolor(theme::CodeBg());
  }

  std::size_t num_end = 0;
  if (is_numbered_result(line, &num_end)) {
    Elements bits;
    if (slice_begin < num_end) {
      const std::size_t hi = std::min(num_end, slice_end);
      bits.push_back(text(std::string(line.substr(slice_begin, hi - slice_begin))) |
                     color(theme::Accent()) | bold);
    }
    if (slice_end > num_end) {
      const std::size_t lo = std::max(num_end, slice_begin);
      bits.push_back(text(std::string(line.substr(lo, slice_end - lo))) | color(theme::FileText()));
    }
    Element row = bits.empty() ? text(" ")
                               : (bits.size() == 1 ? std::move(bits.front()) : hbox(std::move(bits)));
    row = with_indent(1, std::move(row));
    if (hovered) {
      return row | bold | bgcolor(theme::TabHover());
    }
    return row | bgcolor(theme::CodeBg());
  }

  std::size_t lead = 0;
  while (lead < line.size() && line[lead] == ' ') {
    ++lead;
  }
  const int indent_from_spaces = static_cast<int>(lead / 2);
  const int reply_indent = std::max(1, indent_from_spaces);

  // Código: solo si el slice sigue en la zona indentada de firma.
  if (lead >= 4 && slice_begin < line.size()) {
    const std::string_view body = line.substr(lead);
    if (looks_like_code_snippet(body) && slice_begin >= lead) {
      return with_indent(std::max(2, reply_indent),
                         highlight_code_snippet(line.substr(slice_begin, slice_end - slice_begin))) |
             bgcolor(theme::CodeBg());
    }
  }

  // Reply body / narración principal.
  return with_indent(reply_indent,
                     render_reply_body_slice(line, slice_begin, slice_end, /*first=*/true)) |
         bgcolor(theme::CodeBg());
}

}  // namespace tuide
