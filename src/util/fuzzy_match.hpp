#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace tuide {

struct FuzzyMatchResult {
  bool matched = false;
  int score = 0;
  std::vector<std::size_t> indices;
};

FuzzyMatchResult fuzzy_match(std::string_view haystack, std::string_view query);

std::string fuzzy_to_lower(std::string_view text);

FuzzyMatchResult fuzzy_match_cached(std::string_view haystack, std::string_view haystack_lower,
                                    std::string_view query_lower);

}  // namespace tuide
