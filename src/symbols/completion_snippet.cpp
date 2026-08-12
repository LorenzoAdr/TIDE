#include "symbols/completion_snippet.hpp"

#include "editor/editor_state.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace tuide {

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

std::string base_name_before_paren(const std::string& text) {
  const std::size_t open = text.find('(');
  if (open == std::string::npos) {
    return text;
  }
  return text.substr(0, open);
}

bool call_parens_are_empty(const std::string& text) {
  const std::size_t open = text.find('(');
  if (open == std::string::npos) {
    return false;
  }
  const std::size_t close = find_matching_close_paren(text, open);
  if (close == std::string::npos) {
    return false;
  }
  const std::string inside = trim_copy(text.substr(open + 1, close - open - 1));
  if (inside.empty()) {
    return true;
  }
  return inside == "$0" || inside == "${0}" || inside == "${0:}";
}

}  // namespace

bool has_char_at(const std::string& line, int col, char expected) {
  return col >= 0 && col < static_cast<int>(line.size()) &&
         line[static_cast<std::size_t>(col)] == expected;
}

bool completion_insert_is_empty_call(const std::string& insert_text) {
  const std::size_t open = insert_text.find('(');
  if (open == std::string::npos) {
    return false;
  }
  std::size_t depth = 0;
  std::size_t close = std::string::npos;
  for (std::size_t i = open; i < insert_text.size(); ++i) {
    if (insert_text[i] == '(') {
      ++depth;
    } else if (insert_text[i] == ')') {
      --depth;
      if (depth == 0) {
        close = i;
        break;
      }
    }
  }
  if (close == std::string::npos) {
    return false;
  }
  std::string inside = insert_text.substr(open + 1, close - open - 1);
  while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.front()))) {
    inside.erase(inside.begin());
  }
  while (!inside.empty() && std::isspace(static_cast<unsigned char>(inside.back()))) {
    inside.pop_back();
  }
  if (inside.empty()) {
    return true;
  }
  return inside == "$0" || inside == "${0}" || inside == "${0:}";
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
  result.placeholders.reserve(stops.size());
  for (const TabStop& stop : stops) {
    SnippetPlaceholder placeholder;
    placeholder.index = stop.index;
    placeholder.line_offset = stop.line_offset;
    placeholder.col = stop.col;
    placeholder.length = stop.length;
    result.placeholders.push_back(placeholder);
  }
  apply_tab_stop_choice(&result, stops, line, col);
  if (!stops.empty()) {
    int best_index = 999;
    for (const TabStop& stop : stops) {
      if (stop.index == 0) {
        continue;
      }
      if (stop.index < best_index) {
        best_index = stop.index;
        result.active_placeholder_index = stop.index;
      }
    }
    if (best_index == 999) {
      for (const TabStop& stop : stops) {
        if (stop.index == 0) {
          result.active_placeholder_index = 0;
          break;
        }
      }
    }
  }
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
  result.placeholders = inner.placeholders;
  for (SnippetPlaceholder& placeholder : result.placeholders) {
    if (placeholder.line_offset == inner.caret_line_offset) {
      placeholder.col += offset;
    }
  }
  result.active_placeholder_index = inner.active_placeholder_index;
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

  const std::string& signature = detail.empty() ? insert_text : detail;
  const std::string arg_snippet = build_argument_snippet_body(signature);
  const bool has_parens = insert_text.find('(') != std::string::npos;
  const bool empty_parens = has_parens && call_parens_are_empty(insert_text);
  const std::string base = has_parens ? base_name_before_paren(insert_text) : insert_text;

  if (!has_parens || empty_parens) {
    if (paren_already_there) {
      if (arg_snippet.empty()) {
        result.text = base;
        result.caret_col = static_cast<int>(base.size()) + 1;
        return result;
      }
      return adjust_snippet_for_existing_open_paren(base + "(" + arg_snippet + ")");
    }

    if (arg_snippet.empty()) {
      result.text = base + "()";
      result.caret_col = static_cast<int>(result.text.size()) - 1;
      return result;
    }

    return expand_snippet(base + "(" + arg_snippet + ")");
  }

  if (insert_text.find('$') != std::string::npos) {
    SnippetResult expanded = expand_snippet(insert_text);
    if (paren_already_there) {
      return adjust_snippet_for_existing_open_paren(insert_text);
    }
    return expanded;
  }

  return result;
}

namespace {

bool snippet_has_numbered_placeholder(const std::vector<SnippetPlaceholder>& placeholders) {
  for (const SnippetPlaceholder& placeholder : placeholders) {
    if (placeholder.index > 0) {
      return true;
    }
  }
  return false;
}

int snippet_placeholder_sort_key(int index) {
  return index == 0 ? 100000 : index;
}

SnippetSessionStop* current_session_placeholder(SnippetSession* session) {
  if (session == nullptr) {
    return nullptr;
  }
  for (auto& placeholder : session->placeholders) {
    if (placeholder.index == session->current_index) {
      return &placeholder;
    }
  }
  return nullptr;
}

SnippetSessionStop absolute_placeholder(int insert_line, int insert_start_col,
                                        const SnippetPlaceholder& rel) {
  SnippetSessionStop abs;
  abs.index = rel.index;
  abs.line = insert_line + rel.line_offset;
  abs.col = (rel.line_offset == 0 ? insert_start_col : 0) + rel.col;
  abs.length = rel.length;
  return abs;
}

void jump_to_snippet_placeholder(EditorBuffer* buffer, const SnippetSessionStop& placeholder) {
  buffer->reset_to_single_cursor(placeholder.line, placeholder.col);
  if (placeholder.length > 0) {
    buffer->primary().anchor = {placeholder.line, placeholder.col};
    buffer->primary().head = {placeholder.line, placeholder.col + placeholder.length};
  }
  clamp_all_cursors(buffer);
}

}  // namespace

void reflow_snippet_placeholders_after_current_edit(EditorBuffer* buffer, SnippetSession* session) {
  if (buffer == nullptr || session == nullptr) {
    return;
  }
  SnippetSessionStop* current = current_session_placeholder(session);
  if (current == nullptr) {
    return;
  }

  const MultiCursor& cursor = buffer->primary();
  int start_line = current->line;
  int start_col = current->col;
  int end_line = current->line;
  int end_col = current->col;
  if (cursor.has_selection()) {
    cursor.normalized_range(&start_line, &start_col, &end_line, &end_col);
  } else {
    end_line = cursor.head.line;
    end_col = cursor.head.col;
  }

  if (start_line != current->line || end_line != current->line) {
    clear_snippet_session(session);
    return;
  }

  const int new_length = std::max(0, end_col - current->col);
  const int delta = new_length - current->length;
  current->length = new_length;
  if (delta == 0) {
    return;
  }

  for (auto& placeholder : session->placeholders) {
    if (placeholder.index == current->index) {
      continue;
    }
    if (placeholder.line != current->line || placeholder.col <= current->col) {
      continue;
    }
    placeholder.col += delta;
  }
}

void begin_snippet_session(SnippetSession* session, int insert_line, int insert_start_col,
                           const SnippetResult& snippet) {
  clear_snippet_session(session);
  if (session == nullptr || snippet.placeholders.empty() ||
      !snippet_has_numbered_placeholder(snippet.placeholders)) {
    return;
  }

  session->active = true;
  session->current_index = snippet.active_placeholder_index;
  session->placeholders.reserve(snippet.placeholders.size());
  for (const SnippetPlaceholder& rel : snippet.placeholders) {
    session->placeholders.push_back(absolute_placeholder(insert_line, insert_start_col, rel));
  }
}

void clear_snippet_session(SnippetSession* session) {
  if (session == nullptr) {
    return;
  }
  session->active = false;
  session->placeholders.clear();
  session->current_index = 0;
}

bool snippet_session_active(const SnippetSession& session) {
  return session.active && !session.placeholders.empty();
}

bool advance_snippet_session(EditorBuffer* buffer, SnippetSession* session) {
  if (buffer == nullptr || session == nullptr || !snippet_session_active(*session)) {
    return false;
  }

  reflow_snippet_placeholders_after_current_edit(buffer, session);
  if (!snippet_session_active(*session)) {
    return false;
  }

  std::vector<SnippetSessionStop> ordered = session->placeholders;
  std::sort(ordered.begin(), ordered.end(), [](const SnippetSessionStop& a,
                                               const SnippetSessionStop& b) {
    return snippet_placeholder_sort_key(a.index) < snippet_placeholder_sort_key(b.index);
  });

  const SnippetSessionStop* next = nullptr;
  for (const SnippetSessionStop& placeholder : ordered) {
    if (snippet_placeholder_sort_key(placeholder.index) >
        snippet_placeholder_sort_key(session->current_index)) {
      next = &placeholder;
      break;
    }
  }

  if (next == nullptr) {
    clear_snippet_session(session);
    return false;
  }

  session->current_index = next->index;
  jump_to_snippet_placeholder(buffer, *next);
  if (next->index == 0) {
    clear_snippet_session(session);
  }
  return true;
}

}  // namespace tuide
