#include "symbols/completion_snippet.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <vector>

namespace tgdb {

namespace {

struct TabStop {
  int index = 0;
  int line_offset = 0;
  int col = 0;
  int length = 0;
};

bool is_identifier_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string trim_copy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

int parse_snippet_index(const std::string& snippet, std::size_t* pos) {
  int value = 0;
  while (*pos < snippet.size() && std::isdigit(static_cast<unsigned char>(snippet[*pos]))) {
    value = value * 10 + (snippet[*pos] - '0');
    ++(*pos);
  }
  return value;
}

}  // namespace

SnippetResult expand_snippet(const std::string& snippet) {
  SnippetResult result;
  std::string out;
  std::vector<TabStop> stops;

  int line = 0;
  int col = 0;
  auto push_char = [&](char c) {
    out.push_back(c);
    if (c == '\n') {
      ++line;
      col = 0;
    } else {
      ++col;
    }
  };

  for (std::size_t i = 0; i < snippet.size();) {
    if (snippet[i] == '\\' && i + 1 < snippet.size()) {
      push_char(snippet[i + 1]);
      i += 2;
      continue;
    }
    if (snippet[i] != '$') {
      push_char(snippet[i]);
      ++i;
      continue;
    }

    ++i;
    if (i >= snippet.size()) {
      break;
    }

    if (snippet[i] == '{') {
      ++i;
      const int index = parse_snippet_index(snippet, &i);
      std::string placeholder;
      if (i < snippet.size() && snippet[i] == ':') {
        ++i;
        int brace_depth = 1;
        while (i < snippet.size() && brace_depth > 0) {
          if (snippet[i] == '{') {
            ++brace_depth;
          } else if (snippet[i] == '}') {
            --brace_depth;
            if (brace_depth == 0) {
              break;
            }
          }
          if (brace_depth > 0) {
            placeholder.push_back(snippet[i]);
          }
          ++i;
        }
      }
      if (i < snippet.size() && snippet[i] == '}') {
        ++i;
      }

      TabStop stop;
      stop.index = index;
      stop.line_offset = line;
      stop.col = col;
      stop.length = static_cast<int>(placeholder.size());
      stops.push_back(stop);
      for (char c : placeholder) {
        push_char(c);
      }
      continue;
    }

    if (!std::isdigit(static_cast<unsigned char>(snippet[i]))) {
      push_char('$');
      push_char(snippet[i]);
      ++i;
      continue;
    }

    const int index = parse_snippet_index(snippet, &i);
    TabStop stop;
    stop.index = index;
    stop.line_offset = line;
    stop.col = col;
    stop.length = 0;
    stops.push_back(stop);
  }

  result.text = out;

  const TabStop* chosen = nullptr;
  int best_index = 999;
  for (const TabStop& stop : stops) {
    if (stop.index == 0) {
      continue;
    }
    if (stop.index < best_index) {
      best_index = stop.index;
      chosen = &stop;
    }
  }
  if (chosen == nullptr) {
    for (const TabStop& stop : stops) {
      if (stop.index == 0) {
        chosen = &stop;
        break;
      }
    }
  }
  if (chosen != nullptr) {
    result.caret_line_offset = chosen->line_offset;
    result.caret_col = chosen->col;
    if (chosen->length > 0) {
      result.sel_start_col = chosen->col;
      result.sel_end_col = chosen->col + chosen->length;
    }
  } else {
    result.caret_line_offset = line;
    result.caret_col = col;
  }

  return result;
}

SnippetResult finalize_function_call_insert(const std::string& insert_text,
                                            const std::string& detail, bool is_callable) {
  SnippetResult result;
  result.text = insert_text;
  result.caret_col = static_cast<int>(insert_text.size());

  if (!is_callable || insert_text.find('(') != std::string::npos) {
    return result;
  }

  const std::string& signature = detail.empty() ? insert_text : detail;
  const std::size_t open_paren = signature.find('(');
  if (open_paren == std::string::npos) {
    result.text = insert_text + "()";
    result.caret_col = static_cast<int>(result.text.size()) - 1;
    return result;
  }

  const std::size_t close_paren = signature.find(')', open_paren + 1);
  if (close_paren == std::string::npos) {
    result.text = insert_text + "()";
    result.caret_col = static_cast<int>(result.text.size()) - 1;
    return result;
  }

  std::string args = trim_copy(signature.substr(open_paren + 1, close_paren - open_paren - 1));
  if (args.empty() || args == "void") {
    result.text = insert_text + "()";
    result.caret_col = static_cast<int>(result.text.size()) - 1;
    return result;
  }

  result.text = insert_text + "()";
  result.caret_col = static_cast<int>(insert_text.size()) + 1;
  return result;
}

}  // namespace tgdb
