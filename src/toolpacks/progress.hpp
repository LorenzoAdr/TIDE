#pragma once

#include <algorithm>
#include <functional>
#include <string_view>

namespace tuide::toolpacks {

// percent: 0..100; label: optional short detail for the busy strip (e.g. pack id).
using ProgressFn = std::function<void(int percent, std::string_view label)>;

inline void report_progress(const ProgressFn& fn, int percent,
                            std::string_view label = {}) {
  if (!fn) {
    return;
  }
  fn(std::clamp(percent, 0, 100), label);
}

// Remap local 0..100 into [base, base+span] of an overall job.
inline ProgressFn nest_progress(ProgressFn outer, int base, int span,
                                std::string_view default_label = {}) {
  if (!outer) {
    return {};
  }
  base = std::clamp(base, 0, 100);
  span = std::clamp(span, 0, 100 - base);
  return [outer = std::move(outer), base, span, default_label](int percent,
                                                              std::string_view label) {
    const int mapped = base + (std::clamp(percent, 0, 100) * span) / 100;
    report_progress(outer, mapped, label.empty() ? default_label : label);
  };
}

}  // namespace tuide::toolpacks
