#include "util/markdown_html.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_language.hpp"
#include "util/syntax_scope.hpp"

namespace tuide {
namespace {

std::string escape_html(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

// First fence token, trimmed and lowercased (e.g. " Mermaid " -> "mermaid").
std::string normalize_fence_lang(const std::string& raw) {
  std::size_t start = 0;
  while (start < raw.size() &&
         std::isspace(static_cast<unsigned char>(raw[start]))) {
    ++start;
  }
  std::size_t end = start;
  while (end < raw.size() &&
         !std::isspace(static_cast<unsigned char>(raw[end]))) {
    ++end;
  }
  std::string out;
  out.reserve(end - start);
  for (std::size_t i = start; i < end; ++i) {
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(raw[i]))));
  }
  return out;
}

bool is_mermaid_fence(const std::string& fence_alias) {
  return normalize_fence_lang(fence_alias) == "mermaid";
}

const char* css_class_for_scope(SyntaxScope scope) {
  switch (scope) {
    case SyntaxScope::kComment:
      return "tok-comment";
    case SyntaxScope::kString:
      return "tok-string";
    case SyntaxScope::kNumber:
      return "tok-number";
    case SyntaxScope::kKeyword:
      return "tok-keyword";
    case SyntaxScope::kMacro:
      return "tok-macro";
    case SyntaxScope::kNamespace:
      return "tok-namespace";
    case SyntaxScope::kType:
      return "tok-type";
    case SyntaxScope::kFunction:
      return "tok-function";
    case SyntaxScope::kParameter:
      return "tok-parameter";
    case SyntaxScope::kProperty:
      return "tok-property";
    case SyntaxScope::kVariable:
      return "tok-variable";
    case SyntaxScope::kOperator:
      return "tok-operator";
    case SyntaxScope::kDefault:
      break;
  }
  return nullptr;
}

std::vector<std::string> split_source_lines(const std::string& source) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      std::string line = source.substr(start, i - start);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(std::move(line));
      start = i + 1;
    }
  }
  std::string last = source.substr(start);
  if (!last.empty() && last.back() == '\r') {
    last.pop_back();
  }
  lines.push_back(std::move(last));
  return lines;
}

std::string highlight_line_html(const std::string& line, const LineHighlights& highlights) {
  if (highlights.spans.empty()) {
    return escape_html(line);
  }
  std::string out;
  int col = 0;
  const int n = static_cast<int>(line.size());
  for (const HighlightSpan& span : highlights.spans) {
    if (span.start_col > n) {
      continue;
    }
    const int clamped_end = std::min(span.end_col, n);
    if (span.start_col < col) {
      continue;
    }
    if (span.start_col > col) {
      out += escape_html(line.substr(static_cast<std::size_t>(col),
                                     static_cast<std::size_t>(span.start_col - col)));
    }
    if (clamped_end > span.start_col) {
      const SyntaxScope scope = SyntaxScopeForTreeSitterCapture(span.capture);
      const char* css = css_class_for_scope(scope);
      const std::string piece =
          escape_html(line.substr(static_cast<std::size_t>(span.start_col),
                                  static_cast<std::size_t>(clamped_end - span.start_col)));
      if (css != nullptr && scope != SyntaxScope::kDefault) {
        out += "<span class=\"";
        out += css;
        out += "\">";
        out += piece;
        out += "</span>";
      } else {
        out += piece;
      }
      col = clamped_end;
    }
  }
  if (col < n) {
    out += escape_html(line.substr(static_cast<std::size_t>(col)));
  }
  return out;
}

std::string highlight_code_html(const std::string& code, const std::string& fence_alias) {
  const TreeSitterLangKind lang = tree_sitter_lang_kind_for_alias(fence_alias);
  const TSLanguage* ts_lang = tree_sitter_language_for_kind(lang);
  if (ts_lang == nullptr) {
    return escape_html(code);
  }

  TSParser* parser = ts_parser_new();
  if (parser == nullptr) {
    return escape_html(code);
  }
  ts_parser_set_language(parser, ts_lang);
  TSTree* tree = ts_parser_parse_string(parser, nullptr, code.c_str(),
                                        static_cast<uint32_t>(code.size()));
  ts_parser_delete(parser);
  if (tree == nullptr) {
    return escape_html(code);
  }

  const TSNode root = ts_tree_root_node(tree);
  const std::vector<LineHighlights> per_line = highlights_for_document(root, code, lang);
  ts_tree_delete(tree);

  const std::vector<std::string> lines = split_source_lines(code);
  std::ostringstream out;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      out << '\n';
    }
    if (i < per_line.size()) {
      out << highlight_line_html(lines[i], per_line[i]);
    } else {
      out << escape_html(lines[i]);
    }
  }
  return out.str();
}

std::string apply_inline(const std::string& escaped) {
  std::string r = escaped;
  auto replace_pattern = [&](const std::string& open_tok, const std::string& close_tok,
                              const std::string& open_tag, const std::string& close_tag) {
    std::size_t pos = 0;
    while ((pos = r.find(open_tok, pos)) != std::string::npos) {
      const std::size_t end = r.find(close_tok, pos + open_tok.size());
      if (end == std::string::npos) {
        break;
      }
      r = r.substr(0, pos) + open_tag +
          r.substr(pos + open_tok.size(), end - pos - open_tok.size()) +
          close_tag + r.substr(end + close_tok.size());
      pos += open_tag.size();
    }
  };

  std::size_t pos = 0;
  while ((pos = r.find('[', pos)) != std::string::npos) {
    const std::size_t close_text = r.find(']', pos + 1);
    if (close_text == std::string::npos || close_text + 1 >= r.size() || r[close_text + 1] != '(') {
      ++pos;
      continue;
    }
    const std::size_t close_url = r.find(')', close_text + 2);
    if (close_url == std::string::npos) {
      break;
    }
    const std::string text = r.substr(pos + 1, close_text - pos - 1);
    const std::string url = r.substr(close_text + 2, close_url - close_text - 2);
    const std::string tag = "<a href=\"" + url + "\">" + text + "</a>";
    r = r.substr(0, pos) + tag + r.substr(close_url + 1);
    pos += tag.size();
  }

  replace_pattern("`", "`", "<code>", "</code>");
  replace_pattern("**", "**", "<strong>", "</strong>");
  replace_pattern("__", "__", "<strong>", "</strong>");
  replace_pattern("*", "*", "<em>", "</em>");
  replace_pattern("_", "_", "<em>", "</em>");
  return r;
}

bool is_ul_item(const std::string& line, std::string* item) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i + 1 >= line.size()) {
    return false;
  }
  if ((line[i] == '-' || line[i] == '*' || line[i] == '+') && line[i + 1] == ' ') {
    *item = line.substr(i + 2);
    return true;
  }
  return false;
}

bool is_ol_item(const std::string& line, std::string* item) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
    ++i;
  }
  if (i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) {
    return false;
  }
  while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
    ++i;
  }
  if (i + 1 < line.size() && line[i] == '.' && line[i + 1] == ' ') {
    *item = line.substr(i + 2);
    return true;
  }
  return false;
}

bool is_blockquote(const std::string& line, std::string* item) {
  if (line.rfind("> ", 0) == 0) {
    *item = line.substr(2);
    return true;
  }
  if (line == ">") {
    *item = {};
    return true;
  }
  return false;
}

std::string trim_copy(const std::string& text) {
  std::size_t begin = 0;
  while (begin < text.size() &&
         std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return text.substr(begin, end - begin);
}

bool contains_table_pipe(const std::string& line) {
  bool escaped = false;
  bool in_code = false;
  for (const char c : line) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '`') {
      in_code = !in_code;
    } else if (c == '|' && !in_code) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> split_table_cells(const std::string& line) {
  const std::string trimmed = trim_copy(line);
  std::vector<std::string> cells;
  std::string cell;
  bool escaped = false;
  bool in_code = false;
  for (const char c : trimmed) {
    if (escaped) {
      if (c != '|') {
        cell.push_back('\\');
      }
      cell.push_back(c);
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '`') {
      in_code = !in_code;
      cell.push_back(c);
    } else if (c == '|' && !in_code) {
      cells.push_back(trim_copy(cell));
      cell.clear();
    } else {
      cell.push_back(c);
    }
  }
  if (escaped) {
    cell.push_back('\\');
  }
  cells.push_back(trim_copy(cell));

  if (!trimmed.empty() && trimmed.front() == '|' && !cells.empty()) {
    cells.erase(cells.begin());
  }
  if (!trimmed.empty() && trimmed.back() == '|' && !cells.empty() &&
      cells.back().empty()) {
    cells.pop_back();
  }
  return cells;
}

enum class TableAlignment {
  kDefault,
  kLeft,
  kCenter,
  kRight,
};

bool parse_table_delimiter(const std::string& line,
                           std::vector<TableAlignment>* alignments) {
  if (!contains_table_pipe(line)) {
    return false;
  }
  const std::vector<std::string> cells = split_table_cells(line);
  if (cells.empty()) {
    return false;
  }

  std::vector<TableAlignment> parsed;
  parsed.reserve(cells.size());
  for (std::string cell : cells) {
    cell = trim_copy(cell);
    const bool left = !cell.empty() && cell.front() == ':';
    const bool right = !cell.empty() && cell.back() == ':';
    if (left) {
      cell.erase(cell.begin());
    }
    if (right && !cell.empty()) {
      cell.pop_back();
    }
    if (cell.size() < 3 ||
        cell.find_first_not_of('-') != std::string::npos) {
      return false;
    }
    parsed.push_back(left && right ? TableAlignment::kCenter
                                  : right ? TableAlignment::kRight
                                          : left ? TableAlignment::kLeft
                                                 : TableAlignment::kDefault);
  }
  *alignments = std::move(parsed);
  return true;
}

const char* table_alignment_class(TableAlignment alignment) {
  switch (alignment) {
    case TableAlignment::kLeft:
      return " class=\"align-left\"";
    case TableAlignment::kCenter:
      return " class=\"align-center\"";
    case TableAlignment::kRight:
      return " class=\"align-right\"";
    case TableAlignment::kDefault:
      return "";
  }
  return "";
}

std::string markdown_to_html_impl(const std::string& md, const std::string& title) {
  const std::vector<std::string> lines = split_source_lines(md);
  std::ostringstream body;
  bool in_para = false;
  bool in_code = false;
  bool in_ul = false;
  bool in_ol = false;
  bool in_quote = false;
  bool has_mermaid = false;
  std::string code_lang;
  std::string code_body;

  auto close_para = [&]() {
    if (in_para) {
      body << "</p>\n";
      in_para = false;
    }
  };
  auto close_lists = [&]() {
    if (in_ul) {
      body << "</ul>\n";
      in_ul = false;
    }
    if (in_ol) {
      body << "</ol>\n";
      in_ol = false;
    }
  };
  auto close_quote = [&]() {
    if (in_quote) {
      body << "</blockquote>\n";
      in_quote = false;
    }
  };
  auto close_flow = [&]() {
    close_para();
    close_lists();
    close_quote();
  };
  auto flush_code = [&]() {
    if (is_mermaid_fence(code_lang)) {
      has_mermaid = true;
      // Mermaid reads element textContent; escape so raw <>& stay safe in HTML.
      body << "<pre class=\"mermaid\">" << escape_html(code_body) << "</pre>\n";
    } else {
      body << "<pre><code>" << highlight_code_html(code_body, code_lang)
           << "</code></pre>\n";
    }
    code_body.clear();
    code_lang.clear();
  };

  for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
    const std::string& line = lines[line_index];
    if (!in_code && (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0)) {
      close_flow();
      code_lang = line.substr(3);
      in_code = true;
      code_body.clear();
      continue;
    }
    if (in_code) {
      if (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0) {
        flush_code();
        in_code = false;
      } else {
        if (!code_body.empty()) {
          code_body.push_back('\n');
        }
        code_body += line;
      }
      continue;
    }

    std::vector<TableAlignment> table_alignments;
    if (line_index + 1 < lines.size() && contains_table_pipe(line) &&
        parse_table_delimiter(lines[line_index + 1], &table_alignments)) {
      const std::vector<std::string> header_cells = split_table_cells(line);
      if (header_cells.size() == table_alignments.size()) {
        close_flow();
        body << "<div class=\"table-scroll\"><table>\n<thead><tr>\n";
        for (std::size_t cell_index = 0; cell_index < header_cells.size();
             ++cell_index) {
          body << "<th scope=\"col\""
               << table_alignment_class(table_alignments[cell_index]) << ">"
               << apply_inline(escape_html(header_cells[cell_index]))
               << "</th>\n";
        }
        body << "</tr></thead>\n<tbody>\n";

        line_index += 2;
        while (line_index < lines.size() &&
               contains_table_pipe(lines[line_index])) {
          std::vector<std::string> row_cells =
              split_table_cells(lines[line_index]);
          row_cells.resize(header_cells.size());
          body << "<tr>\n";
          for (std::size_t cell_index = 0; cell_index < header_cells.size();
               ++cell_index) {
            body << "<td"
                 << table_alignment_class(table_alignments[cell_index]) << ">"
                 << apply_inline(escape_html(row_cells[cell_index]))
                 << "</td>\n";
          }
          body << "</tr>\n";
          ++line_index;
        }
        body << "</tbody>\n</table></div>\n";
        --line_index;
        continue;
      }
    }

    if (!line.empty() && line[0] == '#') {
      close_flow();
      int level = 0;
      while (level < static_cast<int>(line.size()) && line[level] == '#') {
        ++level;
      }
      if (level > 6) {
        level = 6;
      }
      const std::size_t text_start = static_cast<std::size_t>(level);
      std::string text = text_start < line.size() ? line.substr(text_start) : "";
      if (!text.empty() && text[0] == ' ') {
        text = text.substr(1);
      }
      body << "<h" << level << ">" << apply_inline(escape_html(text)) << "</h" << level
           << ">\n";
      continue;
    }

    if (line == "---" || line == "***" || line == "___") {
      close_flow();
      body << "<hr>\n";
      continue;
    }

    std::string item;
    if (is_blockquote(line, &item)) {
      close_para();
      close_lists();
      if (!in_quote) {
        body << "<blockquote>";
        in_quote = true;
      } else {
        body << "<br>";
      }
      body << apply_inline(escape_html(item));
      continue;
    }

    if (is_ul_item(line, &item)) {
      close_para();
      close_quote();
      if (in_ol) {
        body << "</ol>\n";
        in_ol = false;
      }
      if (!in_ul) {
        body << "<ul>\n";
        in_ul = true;
      }
      body << "<li>" << apply_inline(escape_html(item)) << "</li>\n";
      continue;
    }

    if (is_ol_item(line, &item)) {
      close_para();
      close_quote();
      if (in_ul) {
        body << "</ul>\n";
        in_ul = false;
      }
      if (!in_ol) {
        body << "<ol>\n";
        in_ol = true;
      }
      body << "<li>" << apply_inline(escape_html(item)) << "</li>\n";
      continue;
    }

    if (line.empty() || line.find_first_not_of(" \t\r") == std::string::npos) {
      close_flow();
      continue;
    }

    close_lists();
    close_quote();
    if (!in_para) {
      body << "<p>";
      in_para = true;
    } else {
      body << " ";
    }
    body << apply_inline(escape_html(line));
  }
  close_flow();
  if (in_code) {
    flush_code();
  }

  const std::string escaped_title = escape_html(title);
  std::string mermaid_tail;
  if (has_mermaid) {
    // CDN load requires network; offline preview keeps the source text visible.
    mermaid_tail =
        "<script src=\"https://cdn.jsdelivr.net/npm/mermaid@11.4.1/dist/mermaid.min.js\">"
        "</script>\n"
        "<script>\n"
        "(function(){\n"
        "  var dark = window.matchMedia && "
        "window.matchMedia('(prefers-color-scheme: dark)').matches;\n"
        "  mermaid.initialize({\n"
        "    startOnLoad: true,\n"
        "    theme: dark ? 'dark' : 'default',\n"
        "    securityLevel: 'strict'\n"
        "  });\n"
        "})();\n"
        "</script>\n";
  }

  return "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
         "<meta charset=\"UTF-8\">\n"
         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         "<title>" + escaped_title + "</title>\n"
         "<style>\n"
         ":root{\n"
         "  --bg:#ffffff; --fg:#1f2328; --muted:#57606a;\n"
         "  --accent:#0969da; --accent-hover:#0550ae;\n"
         "  --heading:#0a3069; --code-fg:#cf222e; --code-bg:#f6f8fa;\n"
         "  --pre-bg:#f6f8fa; --border:#d0d7de; --quote:#0969da; --hr:#d0d7de;\n"
         "  --tok-comment:#6e7781; --tok-string:#0a3069; --tok-number:#0550ae;\n"
         "  --tok-keyword:#cf222e; --tok-macro:#953800; --tok-type:#0550ae;\n"
         "  --tok-function:#8250df; --tok-variable:#1f2328; --tok-property:#953800;\n"
         "  --tok-parameter:#953800; --tok-namespace:#1b7c83; --tok-operator:#57606a;\n"
         "}\n"
         "@media (prefers-color-scheme: dark){\n"
         "  :root{\n"
         "    --bg:#0d1117; --fg:#e6edf3; --muted:#8b949e;\n"
         "    --accent:#58a6ff; --accent-hover:#79c0ff;\n"
         "    --heading:#79c0ff; --code-fg:#ff7b72; --code-bg:#161b22;\n"
         "    --pre-bg:#161b22; --border:#30363d; --quote:#58a6ff; --hr:#30363d;\n"
         "    --tok-comment:#8b949e; --tok-string:#a5d6ff; --tok-number:#79c0ff;\n"
         "    --tok-keyword:#ff7b72; --tok-macro:#ffa657; --tok-type:#79c0ff;\n"
         "    --tok-function:#d2a8ff; --tok-variable:#e6edf3; --tok-property:#ffa657;\n"
         "    --tok-parameter:#ffa657; --tok-namespace:#7ee787; --tok-operator:#8b949e;\n"
         "  }\n"
         "}\n"
         "html,body{background:var(--bg);color:var(--fg);}\n"
         "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Helvetica,Arial,"
         "sans-serif;max-width:860px;margin:2rem auto;padding:0 1.2rem 3rem;line-height:1.65;}\n"
         "h1,h2,h3,h4,h5,h6{color:var(--heading);font-weight:650;line-height:1.25;"
         "margin:1.6em 0 .45em;}\n"
         "h1{font-size:2em;border-bottom:1px solid var(--border);padding-bottom:.3em;}\n"
         "h2{font-size:1.5em;border-bottom:1px solid var(--border);padding-bottom:.25em;}\n"
         "h3{font-size:1.25em;}\n"
         "a{color:var(--accent);text-decoration:none;}\n"
         "a:hover{color:var(--accent-hover);text-decoration:underline;}\n"
         "p{margin:.85em 0;}\n"
         "code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;"
         "font-size:.9em;color:var(--code-fg);background:var(--code-bg);"
         "padding:.15em .4em;border-radius:6px;}\n"
         "pre{background:var(--pre-bg);border:1px solid var(--border);padding:1em 1.1em;"
         "border-radius:8px;overflow-x:auto;}\n"
         "pre code{color:var(--fg);background:none;padding:0;font-size:.88em;}\n"
         "pre.mermaid{background:transparent;border:none;padding:1em 0;text-align:center;"
         "overflow-x:auto;}\n"
         ".tok-comment{color:var(--tok-comment);font-style:italic;}\n"
         ".tok-string{color:var(--tok-string);}\n"
         ".tok-number{color:var(--tok-number);}\n"
         ".tok-keyword{color:var(--tok-keyword);font-weight:650;}\n"
         ".tok-macro{color:var(--tok-macro);}\n"
         ".tok-type{color:var(--tok-type);}\n"
         ".tok-function{color:var(--tok-function);}\n"
         ".tok-variable{color:var(--tok-variable);}\n"
         ".tok-property{color:var(--tok-property);}\n"
         ".tok-parameter{color:var(--tok-parameter);}\n"
         ".tok-namespace{color:var(--tok-namespace);}\n"
         ".tok-operator{color:var(--tok-operator);}\n"
         "blockquote{margin:1em 0;padding:.2em 1em;border-left:4px solid var(--quote);"
         "color:var(--muted);}\n"
         "ul,ol{padding-left:1.6em;}\n"
         "li{margin:.25em 0;}\n"
         ".table-scroll{margin:1em 0;overflow-x:auto;}\n"
         "table{width:100%;border-spacing:0;border-collapse:collapse;font-size:.92em;}\n"
         "th,td{border:1px solid var(--border);padding:.5em .75em;text-align:left;"
         "vertical-align:top;}\n"
         "th{background:var(--code-bg);font-weight:650;}\n"
         "tbody tr:nth-child(even){background:var(--code-bg);}\n"
         "th.align-center,td.align-center{text-align:center;}\n"
         "th.align-right,td.align-right{text-align:right;}\n"
         "hr{border:none;border-top:2px solid var(--hr);margin:1.8em 0;}\n"
         "</style>\n</head>\n<body>\n" + body.str() + mermaid_tail + "</body>\n</html>\n";
}

}  // namespace

std::string markdown_to_html(const std::string& md, const std::string& title) {
  return markdown_to_html_impl(md, title);
}

}  // namespace tuide
