#include "ui/git_diff_view.hpp"

#include <algorithm>
#include <string>

#include "i18n/tr.hpp"
#include "ui/theme.hpp"
#include "util/syntax_highlight.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string format_line_number(int line_no, int width) {
  if (line_no <= 0) {
    return std::string(static_cast<std::size_t>(width), ' ');
  }
  std::string text = std::to_string(line_no);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

Decorator left_bg_for_kind(SideBySideRowKind kind) {
  switch (kind) {
    case SideBySideRowKind::kDeletion:
      return bgcolor(theme::Error());
    case SideBySideRowKind::kModification:
      return bgcolor(theme::Warning());
    default:
      return bgcolor(theme::CodeBg());
  }
}

Decorator right_bg_for_kind(SideBySideRowKind kind) {
  switch (kind) {
    case SideBySideRowKind::kAddition:
      return bgcolor(theme::Success());
    case SideBySideRowKind::kModification:
      return bgcolor(theme::Warning());
    default:
      return bgcolor(theme::CodeBg());
  }
}

std::string slice_line(const std::string& line, int scroll_col, int width) {
  if (scroll_col <= 0) {
    if (width <= 0) {
      return line;
    }
    return line.substr(0, static_cast<std::size_t>(std::min(width, static_cast<int>(line.size()))));
  }
  if (scroll_col >= static_cast<int>(line.size())) {
    return {};
  }
  return line.substr(static_cast<std::size_t>(scroll_col),
                     static_cast<std::size_t>(std::max(0, width)));
}

Element highlight_matches(const std::string& line, int line_index, int col_offset,
                          const std::vector<TextMatch>* find_matches) {
  if (find_matches == nullptr || find_matches->empty()) {
    return line.empty() ? text(" ") : HighlightCodeLineLite(line);
  }

  Elements parts;
  int col = 0;
  const int len = static_cast<int>(line.size());
  auto append_plain = [&](int start, int end) {
    if (end <= start) {
      return;
    }
    parts.push_back(text(line.substr(static_cast<std::size_t>(start),
                                     static_cast<std::size_t>(end - start))));
  };
  auto append_match = [&](int start, int end) {
    if (end <= start) {
      return;
    }
    parts.push_back(text(line.substr(static_cast<std::size_t>(start),
                                     static_cast<std::size_t>(end - start))) |
                    bgcolor(theme::FindMatchBg()));
  };

  std::vector<TextMatch> local;
  for (const TextMatch& match : *find_matches) {
    if (match.line != line_index || match.length <= 0) {
      continue;
    }
    const int match_start = match.col - col_offset;
    const int match_end = match_start + match.length;
    if (match_end <= 0 || match_start >= len) {
      continue;
    }
    TextMatch clipped = match;
    clipped.col = std::max(0, match_start);
    clipped.length = std::min(match_end, len) - clipped.col;
    if (clipped.length > 0) {
      local.push_back(clipped);
    }
  }
  if (local.empty()) {
    return line.empty() ? text(" ") : HighlightCodeLineLite(line);
  }
  std::sort(local.begin(), local.end(),
            [](const TextMatch& a, const TextMatch& b) { return a.col < b.col; });

  for (const TextMatch& match : local) {
    append_plain(col, match.col);
    append_match(match.col, match.col + match.length);
    col = match.col + match.length;
  }
  append_plain(col, len);
  return parts.empty() ? text(" ") : hbox(std::move(parts));
}

std::vector<TextMatch> matches_for_side(const std::vector<TextMatch>* find_matches, int line_index,
                                        int side_start, int side_end, int scroll_col) {
  std::vector<TextMatch> out;
  if (find_matches == nullptr) {
    return out;
  }
  for (const TextMatch& match : *find_matches) {
    if (match.line != line_index || match.length <= 0) {
      continue;
    }
    const int match_end = match.col + match.length;
    if (match_end <= side_start || match.col >= side_end) {
      continue;
    }
    const int clipped_start = std::max(match.col, side_start);
    const int clipped_end = std::min(match_end, side_end);
    TextMatch clipped;
    clipped.line = line_index;
    clipped.col = clipped_start - scroll_col;
    clipped.length = clipped_end - clipped_start;
    if (clipped.length > 0) {
      out.push_back(clipped);
    }
  }
  return out;
}

int combined_separator_offset(const SideBySideDiffRow& row) {
  if (row.left.empty() || row.right.empty() || row.left == row.right) {
    return -1;
  }
  return static_cast<int>(row.left.size()) + 3;
}

Element render_diff_code_cell(const SideBySideDiffRow& row, bool right_side, int line_index,
                              int scroll_col, int width, Decorator bg,
                              const std::vector<TextMatch>* find_matches) {
  const std::string& line_text = right_side ? row.right : row.left;
  const int separator = combined_separator_offset(row);
  int side_start = 0;
  int side_end = static_cast<int>(line_text.size());
  if (separator >= 0) {
    if (right_side) {
      side_start = separator;
      side_end = separator + static_cast<int>(row.right.size());
    } else {
      side_end = static_cast<int>(row.left.size());
    }
  }
  const std::vector<TextMatch> side_matches =
      matches_for_side(find_matches, line_index, side_start, side_end, scroll_col);
  const std::vector<TextMatch>* active_matches = side_matches.empty() ? nullptr : &side_matches;
  const std::string slice = slice_line(line_text, scroll_col, width);
  Element content = highlight_matches(slice, line_index, scroll_col, active_matches);
  return content | size(WIDTH, EQUAL, width) | xflex_shrink | bg;
}

}  // namespace

GitDiffViewRenderResult render_git_diff_viewport(const std::vector<SideBySideDiffRow>& rows,
                                                 int scroll, int visible, int scroll_col,
                                                 int code_width,
                                                 const std::vector<TextMatch>* find_matches,
                                                 int /*active_find_line*/) {
  GitDiffViewRenderResult result;
  if (rows.empty()) {
    result.body = text(i18n::tr("git.diff.no_changes")) | color(theme::Muted()) | flex;
    result.rendered_lines = 1;
    return result;
  }

  const int total = static_cast<int>(rows.size());
  const int end = std::min(total, scroll + std::max(visible, 1));
  const int sep_width = 1;
  const int col_width = std::max(8, (code_width - sep_width) / 2);
  const int gutter_w = 7;

  Elements gutter_rows;
  Elements left_rows;
  Elements sep_rows;
  Elements right_rows;

  gutter_rows.push_back(text(std::string(static_cast<std::size_t>(gutter_w), ' ')) |
                        bgcolor(theme::TabIdle()));
  left_rows.push_back(text(" " + i18n::tr("editor.diff.head_column")) | color(theme::Header()) |
                      bgcolor(theme::TabIdle()) | size(WIDTH, EQUAL, col_width));
  sep_rows.push_back(text(" ") | bgcolor(theme::TabIdle()));
  right_rows.push_back(text(i18n::tr("editor.diff.working_column") + " ") | color(theme::Header()) |
                       bgcolor(theme::TabIdle()) | size(WIDTH, EQUAL, col_width));

  for (int row_index = scroll; row_index < end; ++row_index) {
    const SideBySideDiffRow& row = rows[static_cast<std::size_t>(row_index)];
    const Decorator left_bg = left_bg_for_kind(row.kind);
    const Decorator right_bg = right_bg_for_kind(row.kind);

    const std::string gutter = format_line_number(row.left_line, 3) + " " +
                               format_line_number(row.right_line, 3);
    gutter_rows.push_back(text(gutter) | color(theme::Muted()) | bgcolor(theme::CodeBg()));

    left_rows.push_back(render_diff_code_cell(row, false, row_index, scroll_col, col_width, left_bg,
                                              find_matches));
    sep_rows.push_back(text("│") | color(theme::Muted()) | bgcolor(theme::CodeBg()));
    right_rows.push_back(render_diff_code_cell(row, true, row_index, scroll_col, col_width,
                                               right_bg, find_matches));
  }

  result.rendered_lines = end - scroll;
  result.body = vbox({
                    hbox({
                        vbox(std::move(gutter_rows)) | size(WIDTH, EQUAL, gutter_w),
                        vbox(std::move(left_rows)) | size(WIDTH, EQUAL, col_width) | xflex_shrink,
                        vbox(std::move(sep_rows)) | size(WIDTH, EQUAL, sep_width),
                        vbox(std::move(right_rows)) | flex,
                    }),
                }) |
                flex;
  return result;
}

}  // namespace tgdb
