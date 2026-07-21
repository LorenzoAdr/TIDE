#pragma once

#include <algorithm>
#include <memory>
#include <string>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/util/ref.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

inline Element RenderBlinkInputLine(const std::string& content, int cursor_pos, bool focused,
                                    int sel_start = -1, int sel_end = -1) {
  const Decorator text_style = color(theme::WatchInput());

  if (!focused) {
    return text(content.empty() ? " " : content) | text_style;
  }

  const int n = static_cast<int>(content.size());
  const int pos = std::max(0, std::min(cursor_pos, n));
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

  if (pos >= n) {
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_blink::cell_decorator());
    } else {
      parts.push_back(text(" ") | text_style);
    }
  }

  if (parts.empty()) {
    parts.push_back(cursor_blink::visible() ? text(" ") | cursor_blink::cell_decorator()
                                            : text(" ") | text_style);
  }

  return hbox(std::move(parts));
}

inline Element TransformBlinkInput(InputState state, const std::string& content,
                                   const std::string& placeholder, int cursor_pos,
                                   int sel_start = -1, int sel_end = -1) {
  const Decorator panel = bgcolor(theme::CodeBg());

  if (content.empty()) {
    if (!state.focused) {
      return text(placeholder) | dim | panel;
    }
    return RenderBlinkInputLine(content, 0, true) | panel;
  }

  return RenderBlinkInputLine(content, cursor_pos, state.focused, sel_start, sel_end) | panel;
}

inline InputOption MakeBlinkInputOption(StringRef content, StringRef placeholder,
                                        bool multiline = false,
                                        int* selection_anchor = nullptr) {
  InputOption opt;
  opt.content = std::move(content);
  opt.placeholder = std::move(placeholder);
  opt.multiline = multiline;
  // ftxui::Ref copies owned values by value. Share one int so the transform
  // tracks the same cursor_position that Input updates while typing.
  auto cursor = std::make_shared<int>(0);
  opt.cursor_position = Ref<int>(cursor.get());
  opt.transform = [content = opt.content, placeholder = opt.placeholder,
                   cursor = std::move(cursor), selection_anchor](InputState state) {
    int sel_start = -1;
    int sel_end = -1;
    if (selection_anchor != nullptr && *selection_anchor >= 0 &&
        *selection_anchor != *cursor) {
      sel_start = std::min(*selection_anchor, *cursor);
      sel_end = std::max(*selection_anchor, *cursor);
    }
    return TransformBlinkInput(state, *content, *placeholder, *cursor, sel_start, sel_end);
  };
  return opt;
}

}  // namespace tuide
