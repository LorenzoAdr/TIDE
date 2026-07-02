#include "symbols/completion_snippet.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace tgdb {

namespace {

struct TabStop {
  int index = 0;
  int line_offset = 0;
  int col = 0;
  int length = 0;
};

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

std::vector<std::string> split_signature_params(const std::string& args) {
  std::vector<std::string> params;
  std::string current;
  int depth = 0;
  for (char c : args) {
    if (c == '(' || c == '<' || c == '[' || c == '{') {
      ++depth;
      current.push_back(c);
    } else if (c == ')' || c == '>' || c == ']' || c == '}') {
      --depth;
      current.push_back(c);
    } else if (c == ',' && depth == 0) {
      const std::string trimmed = trim_copy(current);
      if (!trimmed.empty()) {
        params.push_back(trimmed);
      }
      current.clear();
    } else {
      current.push_back(c);
    }
  }
  const std::string trimmed = trim_copy(current);
  if (!trimmed.empty()) {
    params.push_back(trimmed);
  }
  return params;
}

std::string strip_default_argument(std::string param) {
  int depth = 0;
  for (std::size_t i = 0; i < param.size(); ++i) {
    const char c = param[i];
    if (c == '(' || c == '<' || c == '[' || c == '{') {
      ++depth;
    } else if (c == ')' || c == '>' || c == ']' || c == '}') {
      --depth;
    } else if (c == '=' && depth == 0) {
      return trim_copy(param.substr(0, i));
    }
  }
  return trim_copy(param);
}

std::size_t find_matching_close_paren(const std::string& signature, std::size_t open_paren) {
  int depth = 0;
  for (std::size_t i = open_paren; i < signature.size(); ++i) {
    if (signature[i] == '(') {
      ++depth;
    } else if (signature[i] == ')') {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }
  return std::string::npos;
}

std::string build_argument_snippet_body(const std::string& signature) {
  const std::size_t open_paren = signature.find('(');
  if (open_paren == std::string::npos) {
    return {};
  }
  const std::size_t close_paren = find_matching_close_paren(signature, open_paren);
  if (close_paren == std::string::npos) {
    return {};
  }

  const std::string args =
      trim_copy(signature.substr(open_paren + 1, close_paren - open_paren - 1));
  if (args.empty() || args == "void") {
    return {};
  }

  const auto params = split_signature_params(args);
  std::string out;
  int index = 1;
  for (const std::string& param : params) {
    const std::string trimmed = strip_default_argument(param);
    if (trimmed.empty()) {
      continue;
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += "${" + std::to_string(index) + ":" + trimmed + "}";
    ++index;
  }
  return out;
}

void apply_tab_stop_choice(SnippetResult* result, const std::vector<TabStop>& stops, int line,
                           int col) {
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
    result->caret_line_offset = chosen->line_offset;
    result->caret_col = chosen->col;
    if (chosen->length > 0) {
      result->sel_start_col = chosen->col;
      result->sel_end_col = chosen->col + chosen->length;
    }
  } else {
    result->caret_line_offset = line;
    result->caret_col = col;
  }
}

}  // namespace

bool has_char_at(const std::string& line, int col, char expected) {
  return col >= 0 && col < static_cast<int>(line.size()) &&
         line[static_cast<std::size_t>(col)] == expected;
}

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
  apply_tab_stop_choice(&result, stops, line, col);
  return result;
}

SnippetResult adjust_snippet_for_existing_open_paren(const std::string& raw_snippet) {
  const std::size_t open = raw_snippet.find('(');
  if (open == std::string::npos) {
    SnippetResult result;
    result.text = raw_snippet;
    result.caret_col = static_cast<int>(raw_snippet.size()) + 1;
    return result;
  }

  const std::string name = raw_snippet.substr(0, open);
  std::string inside = raw_snippet.substr(open + 1);
  if (!inside.empty() && inside.back() == ')') {
    inside.pop_back();
  }

  if (inside.empty()) {
    SnippetResult result;
    result.text = name;
    result.caret_col = static_cast<int>(name.size()) + 1;
    return result;
  }

  SnippetResult inner = expand_snippet(inside);
  SnippetResult result;
  result.text = name + inner.text;
  const int offset = static_cast<int>(name.size());
  result.caret_line_offset = inner.caret_line_offset;
  result.caret_col = offset + inner.caret_col;
  if (inner.sel_start_col >= 0) {
    result.sel_start_col = offset + inner.sel_start_col;
    result.sel_end_col = offset + inner.sel_end_col;
  }
  return result;
}

SnippetResult finalize_function_call_insert(const std::string& insert_text,
                                            const std::string& detail, const bool is_callable,
                                            const bool paren_already_there) {
  SnippetResult result;
  result.text = insert_text;
  result.caret_col = static_cast<int>(insert_text.size());

  if (!is_callable) {
    return result;
  }

  if (insert_text.find('$') != std::string::npos) {
    SnippetResult expanded = expand_snippet(insert_text);
    if (paren_already_there) {
      return adjust_snippet_for_existing_open_paren(insert_text);
    }
    return expanded;
  }

  if (insert_text.find('(') != std::string::npos) {
    return result;
  }

  const std::string& signature = detail.empty() ? insert_text : detail;
  const std::string arg_snippet = build_argument_snippet_body(signature);

  if (paren_already_there) {
    if (arg_snippet.empty()) {
      result.caret_col = static_cast<int>(insert_text.size()) + 1;
      return result;
    }
    return adjust_snippet_for_existing_open_paren(insert_text + "(" + arg_snippet + ")");
  }

  if (arg_snippet.empty()) {
    result.text = insert_text + "()";
    result.caret_col = static_cast<int>(result.text.size()) - 1;
    return result;
  }

  return expand_snippet(insert_text + "(" + arg_snippet + ")");
}

}  // namespace tgdb
