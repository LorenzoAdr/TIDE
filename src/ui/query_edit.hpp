#pragma once

#include <cctype>
#include <string>

namespace tuide {

inline std::string sanitize_single_line_paste(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    if (ch == '\n' || ch == '\r') {
      break;
    }
    if (ch == '\t') {
      out.push_back(' ');
      continue;
    }
    if (static_cast<unsigned char>(ch) >= 32 && ch != 127) {
      out.push_back(ch);
    }
  }
  return out;
}

inline bool is_query_ident_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '_';
}

inline void delete_query_word_backward(std::string* query) {
  if (query == nullptr || query->empty()) {
    return;
  }
  int pos = static_cast<int>(query->size());
  if (is_query_ident_char((*query)[static_cast<std::size_t>(pos - 1)])) {
    while (pos > 0 && is_query_ident_char((*query)[static_cast<std::size_t>(pos - 1)])) {
      --pos;
    }
  } else {
    while (pos > 0 && !is_query_ident_char((*query)[static_cast<std::size_t>(pos - 1)])) {
      --pos;
    }
    while (pos > 0 && is_query_ident_char((*query)[static_cast<std::size_t>(pos - 1)])) {
      --pos;
    }
  }
  query->resize(static_cast<std::size_t>(pos));
}

}  // namespace tuide
