#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace tgdb {

struct FuzzyCatalogEntryView {
  std::string_view text;
  std::string_view text_lower;
};

struct FuzzyCatalogHit {
  std::size_t index = 0;
  int score = 0;
  std::vector<std::size_t> match_indices;
};

std::vector<FuzzyCatalogHit> fuzzy_filter_catalog(
    const std::vector<FuzzyCatalogEntryView>& entries, std::string_view query_lower,
    std::size_t max_results, std::size_t max_empty_results = 150);

}  // namespace tgdb
