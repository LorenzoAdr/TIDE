#include "util/fuzzy_match.hpp"

#include <cctype>

namespace tgdb {

namespace {

char to_lower_char(char c) {
  return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool is_path_separator(char c) {
  return c == '/' || c == '\\';
}

bool is_word_separator(char c) {
  return is_path_separator(c) || c == '_' || c == '-' || c == '.' || c == ' ';
}

int bonus_for_match(std::string_view haystack, std::size_t index) {
  int bonus = 0;
  if (index == 0) {
    bonus += 8;
  }
  if (index > 0 && is_word_separator(haystack[index - 1])) {
    bonus += 4;
  }
  if (index > 0 && std::islower(static_cast<unsigned char>(haystack[index - 1])) &&
      std::isupper(static_cast<unsigned char>(haystack[index]))) {
    bonus += 2;
  }
  return bonus;
}

FuzzyMatchResult fuzzy_match_impl(std::string_view haystack, std::string_view haystack_lower,
                                  std::string_view query_lower) {
  FuzzyMatchResult result;
  if (query_lower.empty()) {
    result.matched = true;
    return result;
  }
  if (haystack.empty() || haystack.size() != haystack_lower.size()) {
    return result;
  }

  std::size_t query_index = 0;
  std::size_t last_match = static_cast<std::size_t>(-1);
  int score = 0;

  for (std::size_t i = 0; i < haystack_lower.size() && query_index < query_lower.size(); ++i) {
    if (haystack_lower[i] != query_lower[query_index]) {
      continue;
    }

    result.indices.push_back(i);
    if (last_match != static_cast<std::size_t>(-1) && last_match + 1 == i) {
      score += 16;
    } else {
      score += bonus_for_match(haystack, i);
      if (last_match != static_cast<std::size_t>(-1)) {
        score -= static_cast<int>(i - last_match - 1);
      }
    }
    last_match = i;
    ++query_index;
  }

  if (query_index < query_lower.size()) {
    return FuzzyMatchResult{};
  }

  result.matched = true;
  result.score = score;
  return result;
}

}  // namespace

std::string fuzzy_to_lower(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    out.push_back(to_lower_char(c));
  }
  return out;
}

FuzzyMatchResult fuzzy_match(std::string_view haystack, std::string_view query) {
  return fuzzy_match_cached(haystack, fuzzy_to_lower(haystack), fuzzy_to_lower(query));
}

FuzzyMatchResult fuzzy_match_cached(std::string_view haystack, std::string_view haystack_lower,
                                    std::string_view query_lower) {
  return fuzzy_match_impl(haystack, haystack_lower, query_lower);
}

}  // namespace tgdb
