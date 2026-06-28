#include "symbols/local_scope_completions.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <unordered_set>

namespace tgdb {

namespace {

bool is_word_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool is_control_keyword(const std::string& word) {
  static const char* kKeywords[] = {
      "if",        "else",     "for",       "while",    "do",         "switch",
      "case",      "default",  "return",    "break",    "continue",   "goto",
      "try",       "catch",    "throw",     "class",    "struct",     "enum",
      "union",     "namespace", "template", "using",   "typedef",    "operator",
      "new",       "delete",   "public",    "private",  "protected",  "virtual",
      "override",  "final",    "friend",    "extern",   "inline",     "explicit",
      "noexcept",  "concept",  "requires",  "import",   "module",     "co_await",
      "co_return", "co_yield",
  };
  const std::string lower = to_lower(word);
  for (const char* keyword : kKeywords) {
    if (lower == keyword) {
      return true;
    }
  }
  return false;
}

std::string trim_left(const std::string& line) {
  std::size_t start = 0;
  while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }
  return line.substr(start);
}

bool extract_decl_name(const std::string& line, std::string* name) {
  const std::string trimmed = trim_left(line);
  if (trimmed.empty() || trimmed[0] == '#' || trimmed.rfind("//", 0) == 0) {
    return false;
  }
  if (trimmed.rfind("using ", 0) == 0 || trimmed.rfind("typedef ", 0) == 0) {
    return false;
  }
  if (trimmed.rfind("for ", 0) == 0 || trimmed.rfind("if ", 0) == 0 ||
      trimmed.rfind("while ", 0) == 0 || trimmed.rfind("switch ", 0) == 0 ||
      trimmed.rfind("catch ", 0) == 0 || trimmed.rfind("return ", 0) == 0) {
    return false;
  }
  if (trimmed.find(") {") != std::string::npos ||
      (trimmed.find('(') != std::string::npos && trimmed.find('{') != std::string::npos &&
       trimmed.find('(') < trimmed.find('{'))) {
    return false;
  }

  std::string work = trimmed;
  if (work.rfind("static ", 0) == 0) {
    work = trim_left(work.substr(7));
  }
  if (work.rfind("const ", 0) == 0) {
    work = trim_left(work.substr(6));
  }
  if (work.rfind("constexpr ", 0) == 0) {
    work = trim_left(work.substr(10));
  }
  if (work.rfind("mutable ", 0) == 0) {
    work = trim_left(work.substr(8));
  }
  if (work.rfind("volatile ", 0) == 0) {
    work = trim_left(work.substr(9));
  }
  if (work.rfind("unsigned ", 0) == 0 || work.rfind("signed ", 0) == 0) {
    work = trim_left(work.substr(work.find(' ') + 1));
  }

  if (work.rfind("auto ", 0) == 0) {
    work = trim_left(work.substr(5));
    const std::size_t eq = work.find('=');
    if (eq == std::string::npos) {
      return false;
    }
    work = trim_left(work.substr(0, eq));
  }

  const std::size_t semi = work.find(';');
  if (semi != std::string::npos) {
    work = work.substr(0, semi);
  }

  std::size_t cut = work.size();
  const std::size_t eq = work.find('=');
  if (eq != std::string::npos) {
    cut = std::min(cut, eq);
  }
  const std::size_t brace = work.find('{');
  if (brace != std::string::npos) {
    cut = std::min(cut, brace);
  }
  const std::size_t paren = work.find('(');
  if (paren != std::string::npos) {
    cut = std::min(cut, paren);
  }
  work = trim_left(work.substr(0, cut));
  if (work.empty()) {
    return false;
  }

  std::size_t end = work.size();
  while (end > 0 && (std::isspace(static_cast<unsigned char>(work[end - 1])) || work[end - 1] == '*' ||
                     work[end - 1] == '&' || work[end - 1] == ']')) {
    --end;
  }

  std::size_t start = end;
  while (start > 0 && is_word_char(work[start - 1])) {
    --start;
  }
  if (start == end) {
    return false;
  }

  const std::string candidate = work.substr(start, end - start);
  if (candidate.empty() || is_control_keyword(candidate)) {
    return false;
  }
  if (start > 0 && work[start - 1] == ':') {
    return false;
  }

  *name = candidate;
  return true;
}

bool looks_like_function_open(const std::vector<std::string>& lines, int line, int brace_col) {
  std::string before_brace = trim_left(lines[static_cast<std::size_t>(line)].substr(
      0, static_cast<std::size_t>(brace_col)));
  if (line > 0) {
    const std::string prev = trim_left(lines[static_cast<std::size_t>(line - 1)]);
    if (!prev.empty() && prev.back() != ';' && prev.back() != '{' && prev.back() != '}') {
      before_brace = prev + " " + before_brace;
    }
  }
  if (before_brace.empty()) {
    return false;
  }

  const std::string lower = to_lower(before_brace);
  if (lower.find("class ") != std::string::npos || lower.find("struct ") != std::string::npos ||
      lower.find("enum ") != std::string::npos || lower.find("namespace ") != std::string::npos) {
    return false;
  }
  if (lower.rfind("if", 0) == 0 || lower.rfind("for ", 0) == 0 ||
      lower.rfind("while ", 0) == 0 || lower.rfind("switch ", 0) == 0 ||
      lower.rfind("else", 0) == 0 || lower.rfind("catch ", 0) == 0) {
    return false;
  }

  std::string tail = before_brace;
  while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.back()))) {
    tail.pop_back();
  }
  if (tail.empty() || tail.back() != ')') {
    return false;
  }

  const std::size_t open_paren = tail.rfind('(');
  if (open_paren == std::string::npos) {
    return false;
  }

  const std::string before_open = trim_left(tail.substr(0, open_paren));
  if (before_open.empty()) {
    return false;
  }

  const std::size_t space = before_open.find_last_of(" \t");
  const std::string token =
      space == std::string::npos ? before_open : before_open.substr(space + 1);
  return !is_control_keyword(token);
}

bool find_enclosing_class(const std::vector<std::string>& lines, int cursor_line, int cursor_col,
                          int* class_body_line, int* class_body_col) {
  int depth = 0;
  for (int line = cursor_line; line >= 0; --line) {
    const std::string& text = lines[static_cast<std::size_t>(line)];
    int col_start = (line == cursor_line) ? cursor_col : static_cast<int>(text.size());
    for (int col = col_start - 1; col >= 0; --col) {
      const char c = text[static_cast<std::size_t>(col)];
      if (c == '}') {
        ++depth;
      } else if (c == '{') {
        if (depth == 0) {
          std::string header = trim_left(text.substr(0, static_cast<std::size_t>(col)));
          if (line > 0 && header.empty()) {
            header = trim_left(lines[static_cast<std::size_t>(line - 1)]);
          }
          const std::string lower = to_lower(header);
          if (lower.find("class ") != std::string::npos ||
              lower.find("struct ") != std::string::npos) {
            *class_body_line = line;
            *class_body_col = col + 1;
            return true;
          }
        }
        if (depth > 0) {
          --depth;
        }
      }
    }
  }
  return false;
}

bool find_function_body_start(const std::vector<std::string>& lines, int cursor_line,
                              int cursor_col, int* body_line, int* body_col) {
  int depth = 0;
  for (int line = cursor_line; line >= 0; --line) {
    const std::string& text = lines[static_cast<std::size_t>(line)];
    int col_start = (line == cursor_line) ? cursor_col : static_cast<int>(text.size());
    for (int col = col_start - 1; col >= 0; --col) {
      const char c = text[static_cast<std::size_t>(col)];
      if (c == '}') {
        ++depth;
      } else if (c == '{') {
        if (depth == 0 && looks_like_function_open(lines, line, col)) {
          *body_line = line;
          *body_col = col + 1;
          return true;
        }
        if (depth > 0) {
          --depth;
        }
      }
    }
  }
  return false;
}

void collect_scope_names(const std::vector<std::string>& lines, int start_line, int start_col,
                         int end_line, int end_col, int min_depth, int max_depth,
                         const std::string& detail, std::unordered_set<std::string>* names,
                         std::vector<CompletionItem>* items) {
  int depth = 0;
  for (int line = 0; line <= start_line; ++line) {
    const std::string& text = lines[static_cast<std::size_t>(line)];
    const int col_limit =
        (line == start_line) ? start_col : static_cast<int>(text.size());
    for (int col = 0; col < col_limit; ++col) {
      const char c = text[static_cast<std::size_t>(col)];
      if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
      }
    }
  }

  for (int line = start_line; line <= end_line; ++line) {
    const std::string& text = lines[static_cast<std::size_t>(line)];
    const int col_start = (line == start_line) ? start_col : 0;
    const int col_limit = (line == end_line) ? end_col : static_cast<int>(text.size());

    for (int col = col_start; col < col_limit; ++col) {
      const char c = text[static_cast<std::size_t>(col)];
      if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
      }
    }

    if (depth < min_depth || depth > max_depth) {
      continue;
    }

    if (line == end_line) {
      continue;
    }

    const std::string trimmed_line = trim_left(text);
    const std::string trimmed_lower = to_lower(trimmed_line);
    if (trimmed_lower.rfind("class ", 0) == 0 || trimmed_lower.rfind("struct ", 0) == 0 ||
        trimmed_lower.rfind("public:", 0) == 0 || trimmed_lower.rfind("private:", 0) == 0 ||
        trimmed_lower.rfind("protected:", 0) == 0) {
      continue;
    }

    std::string name;
    if (!extract_decl_name(text, &name)) {
      continue;
    }
    if (!names->insert(name).second) {
      continue;
    }

    CompletionItem item;
    item.label = name;
    item.insert_text = name;
    item.kind = SymbolKind::kVariable;
    item.detail = detail;
    if (trim_left(text).rfind("static ", 0) == 0) {
      item.detail = "variable estática local";
    }
    items->push_back(std::move(item));
  }
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

}  // namespace

std::vector<CompletionItem> local_scope_completions(const std::string& text, int line,
                                                      int character) {
  if (text.empty() || line < 0) {
    return {};
  }

  const std::vector<std::string> lines = split_lines(text);
  if (line >= static_cast<int>(lines.size())) {
    return {};
  }

  const int cursor_col = std::max(0, std::min(character, static_cast<int>(lines[line].size())));
  std::unordered_set<std::string> names;
  std::vector<CompletionItem> items;

  int fn_line = 0;
  int fn_col = 0;
  const bool fn_found = find_function_body_start(lines, line, cursor_col, &fn_line, &fn_col);
  if (fn_found) {
    collect_scope_names(lines, fn_line, fn_col, line, cursor_col, 1, 999, "variable local", &names,
                        &items);
  }

  int class_line = 0;
  int class_col = 0;
  const bool class_found = find_enclosing_class(lines, line, cursor_col, &class_line, &class_col);
  if (class_found) {
    collect_scope_names(lines, class_line, class_col, line, cursor_col, 1, 1, "miembro", &names,
                        &items);
  }

  std::sort(items.begin(), items.end(),
            [](const CompletionItem& a, const CompletionItem& b) { return a.label < b.label; });
  return items;
}

void merge_completion_items(std::vector<CompletionItem>* target,
                            const std::vector<CompletionItem>& extras) {
  if (target == nullptr || extras.empty()) {
    return;
  }

  std::unordered_set<std::string> seen;
  seen.reserve(target->size() + extras.size());
  for (const CompletionItem& item : *target) {
    seen.insert(to_lower(item.label));
  }

  for (const CompletionItem& item : extras) {
    if (item.label.empty()) {
      continue;
    }
    if (seen.insert(to_lower(item.label)).second) {
      target->push_back(item);
    }
  }
}

}  // namespace tgdb
