#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace tuide {

inline float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.empty() || a.size() != b.size()) {
    return 0.0f;
  }
  double dot = 0.0;
  double na = 0.0;
  double nb = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double x = static_cast<double>(a[i]);
    const double y = static_cast<double>(b[i]);
    dot += x * y;
    na += x * x;
    nb += y * y;
  }
  if (na <= 0.0 || nb <= 0.0) {
    return 0.0f;
  }
  return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

}  // namespace tuide
