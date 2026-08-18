#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/util/ref.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

inline std::size_t utf8_next_len(const std::string& text, std::size_t i) {
  if (i >= text.size()) {
    return 0;
  }
  const unsigned char byte = static_cast<unsigned char>(text[i]);
  if ((byte & 0x80) == 0) {
    return 1;
  }
  if ((byte & 0xE0) == 0xC0) {
    return 2;
  }
  if ((byte & 0xF0) == 0xE0) {
    return 3;
  }
  if ((byte & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

// Soft-wrap by terminal columns (1 codepoint ≈ 1 column). Hard `\n` breaks a line.
inline std::vector<std::pair<std::size_t, std::size_t>> soft_wrap_ranges(const std::string& text,
                                                                         int width) {
  width = std::max(1, width);
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  std::size_t i = 0;
  while (i < text.size()) {
    const std::size_t start = i;
    if (text[i] == '\n') {
      ranges.emplace_back(start, start);
      ++i;
      continue;
    }
    int cols = 0;
    while (i < text.size() && text[i] != '\n' && cols < width) {
      const std::size_t step = std::max<std::size_t>(1, utf8_next_len(text, i));
      i += step;
      ++cols;
    }
    ranges.emplace_back(start, i);
    if (i < text.size() && text[i] == '\n') {
      ++i;
    }
  }
  if (ranges.empty() || (!text.empty() && text.back() == '\n')) {
    ranges.emplace_back(text.size(), text.size());
  }
  return ranges;
}

inline int soft_wrap_line_count(const std::string& text, int width, int max_lines = 8) {
  if (text.empty()) {
    return 1;
  }
  const int n = static_cast<int>(soft_wrap_ranges(text, width).size());
  return std::max(1, std::min(max_lines, n));
}

inline Element RenderBlinkInputLine(const std::string& content, int cursor_pos, bool focused,
                                    int sel_start = -1, int sel_end = -1,
                                    bool draw_cursor = true) {
  const Decorator text_style = color(theme::WatchInput());

  if (!focused) {
    return text(content.empty() ? " " : content) | text_style;
  }

  const int n = static_cast<int>(content.size());
  const int pos = draw_cursor ? std::max(0, std::min(cursor_pos, n)) : -1;
  int sel_lo = -1;
  int sel_hi = -1;
  if (sel_start >= 0 && sel_end >= 0) {
    sel_lo = std::max(0, std::min(std::min(sel_start, sel_end), n));
    sel_hi = std::max(0, std::min(std::max(sel_start, sel_end), n));
  }
  const bool has_sel = sel_lo >= 0 && sel_hi > sel_lo;

  auto in_sel = [&](int index) { return has_sel && index >= sel_lo && index < sel_hi; };
  auto style_at = [&](int index) -> Decorator {
    if (in_sel(index)) {
      return bgcolor(theme::SelectionBg()) | color(theme::WatchInput());
    }
    return text_style;
  };

  Elements parts;
  int i = 0;
  while (i < n) {
    if (i == pos) {
      Element at = text(content.substr(static_cast<std::size_t>(i), 1));
      if (cursor_blink::visible()) {
        at = at | cursor_blink::cell_decorator();
      } else {
        at = at | style_at(i);
      }
      parts.push_back(std::move(at));
      ++i;
      continue;
    }

    const bool selected_run = in_sel(i);
    int j = i + 1;
    while (j < n && j != pos && in_sel(j) == selected_run) {
      ++j;
    }
    parts.push_back(text(content.substr(static_cast<std::size_t>(i),
                                        static_cast<std::size_t>(j - i))) |
                    style_at(i));
    i = j;
  }

  if (pos >= 0 && pos >= n) {
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_blink::cell_decorator());
    } else {
      parts.push_back(text(" ") | text_style);
    }
  }

  if (parts.empty()) {
    if (draw_cursor && cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_blink::cell_decorator());
    } else {
      parts.push_back(text(" ") | text_style);
    }
  }

  return hbox(std::move(parts));
}

inline Element BlinkInputSurface(Element inner, const Decorator& panel) {
  return hbox({std::move(inner) | xflex_shrink, filler()}) | panel;
}

inline Element TransformBlinkInput(InputState state, const std::string& content,
                                   const std::string& placeholder, int cursor_pos,
                                   int sel_start = -1, int sel_end = -1, int wrap_width = 0,
                                   Color panel_bg = theme::CodeBg()) {
  const Decorator panel = bgcolor(panel_bg);

  if (content.empty()) {
    if (!state.focused) {
      return BlinkInputSurface(text(placeholder) | dim, panel);
    }
    return BlinkInputSurface(RenderBlinkInputLine(content, 0, true), panel);
  }

  if (wrap_width <= 0) {
    return BlinkInputSurface(
        RenderBlinkInputLine(content, cursor_pos, state.focused, sel_start, sel_end), panel);
  }

  const auto ranges = soft_wrap_ranges(content, wrap_width);
  constexpr int kMaxLines = 8;
  Elements rows;
  const int limit = std::min(static_cast<int>(ranges.size()), kMaxLines);
  for (int li = 0; li < limit; ++li) {
    const auto [begin, end] = ranges[static_cast<std::size_t>(li)];
    const std::string segment = content.substr(begin, end - begin);
    const bool cursor_here =
        state.focused && cursor_pos >= static_cast<int>(begin) &&
        (cursor_pos < static_cast<int>(end) ||
         (cursor_pos == static_cast<int>(end) &&
          (li + 1 == limit || cursor_pos == static_cast<int>(content.size()))));
    int local_sel_start = -1;
    int local_sel_end = -1;
    if (sel_start >= 0 && sel_end >= 0) {
      const int lo = std::min(sel_start, sel_end);
      const int hi = std::max(sel_start, sel_end);
      const int seg_lo = static_cast<int>(begin);
      const int seg_hi = static_cast<int>(end);
      if (hi > seg_lo && lo < seg_hi) {
        local_sel_start = std::max(0, lo - seg_lo);
        local_sel_end = std::min(static_cast<int>(segment.size()), hi - seg_lo);
      }
    }
    const int local_cursor =
        cursor_here ? cursor_pos - static_cast<int>(begin) : static_cast<int>(segment.size()) + 1;
    rows.push_back(RenderBlinkInputLine(segment, local_cursor, state.focused, local_sel_start,
                                        local_sel_end, cursor_here));
  }
  if (rows.empty()) {
    rows.push_back(RenderBlinkInputLine({}, 0, state.focused));
  }
  return BlinkInputSurface(vbox(std::move(rows)), panel);
}

inline InputOption MakeBlinkInputOption(StringRef content, StringRef placeholder,
                                        bool multiline = false,
                                        int* selection_anchor = nullptr,
                                        int* wrap_width = nullptr,
                                        Color panel_bg = theme::CodeBg()) {
  InputOption opt;
  opt.content = std::move(content);
  opt.placeholder = std::move(placeholder);
  opt.multiline = multiline;
  // ftxui::Ref copies owned values by value. Share one int so the transform
  // tracks the same cursor_position that Input updates while typing.
  auto cursor = std::make_shared<int>(0);
  opt.cursor_position = Ref<int>(cursor.get());
  opt.transform = [content = opt.content, placeholder = opt.placeholder,
                   cursor = std::move(cursor), selection_anchor, wrap_width,
                   panel_bg](InputState state) {
    int sel_start = -1;
    int sel_end = -1;
    if (selection_anchor != nullptr && *selection_anchor >= 0 &&
        *selection_anchor != *cursor) {
      sel_start = std::min(*selection_anchor, *cursor);
      sel_end = std::max(*selection_anchor, *cursor);
    }
    const int width = wrap_width != nullptr ? *wrap_width : 0;
    return TransformBlinkInput(state, *content, *placeholder, *cursor, sel_start, sel_end, width,
                               panel_bg);
  };
  return opt;
}

}  // namespace tuide
