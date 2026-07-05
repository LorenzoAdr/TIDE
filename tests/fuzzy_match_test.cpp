#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "util/fuzzy_match.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void check_indices(const tgdb::FuzzyMatchResult& result, std::initializer_list<std::size_t> expected) {
  check(result.indices.size() == expected.size(), "index count");
  std::size_t i = 0;
  for (std::size_t index : expected) {
    check(result.indices[i] == index, "index value");
    ++i;
  }
}

void test_empty_query_matches() {
  const auto result = tgdb::fuzzy_match("src/main.cpp", "");
  check(result.matched, "empty query matches");
  check(result.score == 0, "empty query score");
  check(result.indices.empty(), "empty query indices");
}

void test_subsequence_match() {
  const auto result = tgdb::fuzzy_match("MyApp.cpp", "mapp");
  check(result.matched, "mapp matches MyApp.cpp");
  check(!result.indices.empty(), "has indices");
}

void test_path_subsequence() {
  const auto result = tgdb::fuzzy_match("src/editor/application.cpp", "edap");
  check(result.matched, "edap matches path");
}

void test_no_match() {
  const auto result = tgdb::fuzzy_match("main.cpp", "xyz");
  check(!result.matched, "xyz does not match");
}

void test_case_insensitive() {
  const auto result = tgdb::fuzzy_match("Strings_EN.cpp", "str_en");
  check(result.matched, "case insensitive subsequence");
}

void test_consecutive_scores_higher() {
  const auto early = tgdb::fuzzy_match("main.cpp", "main");
  const auto late = tgdb::fuzzy_match("not_main.cpp", "main");
  check(early.matched && late.matched, "both match");
  check(early.score > late.score, "earlier match scores higher");
}

void test_separator_bonus() {
  const auto result = tgdb::fuzzy_match("src/app/main.cpp", "sam");
  check(result.matched, "separator-aware match");
  check_indices(result, {0, 4, 8});
}

void test_cached_matches_plain_api() {
  const std::string haystack = "src/editor/application.cpp";
  const std::string query = "edap";
  const auto plain = tgdb::fuzzy_match(haystack, query);
  const auto cached =
      tgdb::fuzzy_match_cached(haystack, tgdb::fuzzy_to_lower(haystack), tgdb::fuzzy_to_lower(query));
  check(plain.matched == cached.matched, "cached matched flag");
  check(plain.score == cached.score, "cached score");
  check(plain.indices == cached.indices, "cached indices");
}

void test_fuzzy_to_lower() {
  check(tgdb::fuzzy_to_lower("Src/App/Main.CPP") == "src/app/main.cpp", "lower cache");
}

}  // namespace

int main() {
  test_empty_query_matches();
  test_subsequence_match();
  test_path_subsequence();
  test_no_match();
  test_case_insensitive();
  test_consecutive_scores_higher();
  test_separator_bonus();
  test_cached_matches_plain_api();
  test_fuzzy_to_lower();
  return 0;
}
