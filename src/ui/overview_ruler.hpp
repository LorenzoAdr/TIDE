#pragma once

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "editor/text_search.hpp"
#include "lsp/diagnostics.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

enum class OverviewMark {
  kNone = 0,
  kFind = 1,
  kGit = 2,
  kWarning = 3,
  kError = 4,
};

struct OverviewRulerLayout {
  int bar_height = 0;
  int total_lines = 0;
  int scroll = 0;
  int visible_lines = 0;
};

struct OverviewRulerInput {
  int total_lines = 0;
  int scroll = 0;
  int visible_lines = 0;
  const std::unordered_map<int, std::vector<Diagnostic>>* diagnostics_by_line = nullptr;
  const std::unordered_set<int>* git_changed_lines = nullptr;
  const std::vector<TextMatch>* text_matches = nullptr;
};

inline OverviewMark overview_mark_for_line(const OverviewRulerInput& input, int line) {
  if (line < 0 || line >= input.total_lines) {
    return OverviewMark::kNone;
  }

  bool warning = false;
  if (input.diagnostics_by_line != nullptr) {
    const auto it = input.diagnostics_by_line->find(line);
    if (it != input.diagnostics_by_line->end()) {
      for (const auto& item : it->second) {
        if (item.severity == DiagnosticSeverity::kError) {
          return OverviewMark::kError;
        }
        if (item.severity == DiagnosticSeverity::kWarning) {
          warning = true;
        }
      }
    }
  }

  if (input.git_changed_lines != nullptr &&
      input.git_changed_lines->count(line) > 0) {
    return OverviewMark::kGit;
  }

  if (warning) {
    return OverviewMark::kWarning;
  }

  return OverviewMark::kNone;
}

inline int overview_ruler_bucket_for_line(int line, int bar_height, int total_lines) {
  if (bar_height <= 0 || total_lines <= 0 || line < 0) {
    return 0;
  }
  if (total_lines == 1) {
    return 0;
  }
  const int y = (line * bar_height) / total_lines;
  return std::max(0, std::min(y, bar_height - 1));
}

inline int overview_ruler_line_for_y(const OverviewRulerLayout& layout, int local_y) {
  if (layout.bar_height <= 0 || layout.total_lines <= 0) {
    return 0;
  }
  const int clamped_y = std::max(0, std::min(local_y, layout.bar_height - 1));
  if (layout.total_lines == 1) {
    return 0;
  }
  return std::min(layout.total_lines - 1,
                  (clamped_y * layout.total_lines) / layout.bar_height);
}

inline Color overview_mark_color(OverviewMark mark) {
  switch (mark) {
    case OverviewMark::kError:
      return theme::Error();
    case OverviewMark::kWarning:
      return theme::Warning();
    case OverviewMark::kGit:
      return theme::Success();
    case OverviewMark::kFind:
      return theme::Accent();
    case OverviewMark::kNone:
    default:
      return theme::Muted();
  }
}

inline void overview_ruler_place_mark(std::vector<OverviewMark>& buckets, int line,
                                      OverviewMark mark, int bar_height, int total_lines) {
  if (mark == OverviewMark::kNone || line < 0 || line >= total_lines || bar_height <= 0) {
    return;
  }
  const int bucket = overview_ruler_bucket_for_line(line, bar_height, total_lines);
  if (static_cast<int>(mark) >
      static_cast<int>(buckets[static_cast<std::size_t>(bucket)])) {
    buckets[static_cast<std::size_t>(bucket)] = mark;
  }
}

inline std::vector<OverviewMark> build_overview_buckets(const OverviewRulerInput& input,
                                                        int bar_height) {
  std::vector<OverviewMark> buckets(static_cast<std::size_t>(std::max(0, bar_height)),
                                    OverviewMark::kNone);
  if (bar_height <= 0 || input.total_lines <= 0) {
    return buckets;
  }

  for (int line = 0; line < input.total_lines; ++line) {
    const OverviewMark mark = overview_mark_for_line(input, line);
    overview_ruler_place_mark(buckets, line, mark, bar_height, input.total_lines);
  }

  if (input.text_matches != nullptr) {
    for (const auto& match : *input.text_matches) {
      overview_ruler_place_mark(buckets, match.line, OverviewMark::kFind, bar_height,
                                input.total_lines);
    }
  }

  return buckets;
}

inline bool overview_ruler_row_in_viewport(const OverviewRulerLayout& layout, int row) {
  if (layout.total_lines <= 0 || layout.bar_height <= 0 || layout.visible_lines <= 0) {
    return false;
  }
  const int viewport_start =
      overview_ruler_bucket_for_line(layout.scroll, layout.bar_height, layout.total_lines);
  const int last_visible_line =
      std::min(layout.total_lines - 1, layout.scroll + layout.visible_lines - 1);
  const int viewport_end =
      overview_ruler_bucket_for_line(last_visible_line, layout.bar_height, layout.total_lines);
  return row >= viewport_start && row <= viewport_end;
}

inline Element vertical_overview_ruler(const OverviewRulerInput& input, int bar_height,
                                       OverviewRulerLayout* layout_out = nullptr) {
  Elements rows;
  if (bar_height <= 0) {
    if (layout_out != nullptr) {
      *layout_out = {};
    }
    return text("");
  }

  OverviewRulerLayout layout;
  layout.bar_height = bar_height;
  layout.total_lines = input.total_lines;
  layout.scroll = input.scroll;
  layout.visible_lines = input.visible_lines;
  if (layout_out != nullptr) {
    *layout_out = layout;
  }

  const std::vector<OverviewMark> buckets = build_overview_buckets(input, bar_height);
  for (int row = 0; row < bar_height; ++row) {
    const OverviewMark mark = buckets[static_cast<std::size_t>(row)];
    const bool in_viewport = overview_ruler_row_in_viewport(layout, row);
    Element cell;
    if (mark != OverviewMark::kNone) {
      cell = text("▌") | color(overview_mark_color(mark));
    } else {
      cell = text(" ") | color(theme::Muted());
    }
    if (in_viewport) {
      cell = cell | bgcolor(theme::EditorLineHi());
    }
    rows.push_back(std::move(cell));
  }
  return vbox(std::move(rows)) | bgcolor(theme::CodeBg());
}

inline bool overview_ruler_contains(const Box& box, int x, int y) {
  return box.Contain(x, y);
}

}  // namespace tgdb
