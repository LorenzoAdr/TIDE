#include "util/fuzzy_catalog_filter.hpp"

#include <algorithm>

#include "util/fuzzy_match.hpp"

namespace tuide {

namespace {

struct Candidate {
  std::size_t index = 0;
  int score = 0;
  std::vector<std::size_t> match_indices;
};

}  // namespace

std::vector<FuzzyCatalogHit> fuzzy_filter_catalog(
    const std::vector<FuzzyCatalogEntryView>& entries, std::string_view query_lower,
    const std::size_t max_results, const std::size_t max_empty_results) {
  if (entries.empty()) {
    return {};
  }

  if (query_lower.empty()) {
    std::vector<FuzzyCatalogHit> results;
    const std::size_t limit = std::min(max_empty_results, entries.size());
    results.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
      results.push_back({i, 0, {}});
    }
    return results;
  }

  std::vector<Candidate> candidates;
  candidates.reserve(std::min(entries.size(), std::size_t{512}));

  for (std::size_t i = 0; i < entries.size(); ++i) {
    const FuzzyCatalogEntryView& entry = entries[i];
    if (entry.text.empty() || entry.text_lower.empty()) {
      continue;
    }
    const FuzzyMatchResult result = fuzzy_match_cached(entry.text, entry.text_lower, query_lower);
    if (!result.matched) {
      continue;
    }
    candidates.push_back({i, result.score, result.indices});
  }

  std::sort(candidates.begin(), candidates.end(),
            [&entries](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) {
      return a.score > b.score;
    }
    const std::string_view ta = entries[a.index].text;
    const std::string_view tb = entries[b.index].text;
    if (ta.size() != tb.size()) {
      return ta.size() < tb.size();
    }
    return ta < tb;
  });

  if (candidates.size() > max_results) {
    candidates.resize(max_results);
  }

  std::vector<FuzzyCatalogHit> results;
  results.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    results.push_back({candidate.index, candidate.score, candidate.match_indices});
  }
  return results;
}

}  // namespace tuide
