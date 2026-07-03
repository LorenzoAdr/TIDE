#pragma once

#include <algorithm>
#include <string>

#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/util/ref.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

inline Element RenderBlinkInputLine(const std::string& content, int cursor_pos, bool focused) {
  const Decorator text_style = color(theme::WatchInput());

  if (!focused) {
    return text(content.empty() ? " " : content) | text_style;
  }

  const int pos = std::max(0, std::min(cursor_pos, static_cast<int>(content.size())));
  Elements parts;

  if (pos > 0) {
    parts.push_back(text(content.substr(0, static_cast<std::size_t>(pos))) | text_style);
  }

  if (pos >= static_cast<int>(content.size())) {
    if (cursor_blink::visible()) {
      parts.push_back(text(" ") | cursor_blink::cell_decorator());
    } else {
      parts.push_back(text(" ") | text_style);
    }
  } else {
    Element at = text(content.substr(static_cast<std::size_t>(pos), 1));
    if (cursor_blink::visible()) {
      at = at | cursor_blink::cell_decorator();
    } else {
      at = at | text_style;
    }
    parts.push_back(std::move(at));
    if (pos + 1 < static_cast<int>(content.size())) {
      parts.push_back(text(content.substr(static_cast<std::size_t>(pos + 1))) | text_style);
    }
  }

  if (parts.empty()) {
    parts.push_back(cursor_blink::visible() ? text(" ") | cursor_blink::cell_decorator()
                                            : text(" ") | text_style);
  }

  return hbox(std::move(parts));
}

inline Element TransformBlinkInput(InputState state, const std::string& content,
                                   const std::string& placeholder, int cursor_pos) {
  const Decorator panel = bgcolor(theme::CodeBg());

  if (content.empty()) {
    if (!state.focused) {
      return text(placeholder) | dim | panel;
    }
    return RenderBlinkInputLine(content, 0, true) | panel;
  }

  return RenderBlinkInputLine(content, cursor_pos, state.focused) | panel;
}

inline InputOption MakeBlinkInputOption(StringRef content, StringRef placeholder,
                                        bool multiline = false) {
  InputOption opt;
  opt.content = std::move(content);
  opt.placeholder = std::move(placeholder);
  opt.multiline = multiline;
  Ref<int> cursor_pos = opt.cursor_position;
  opt.transform = [content = opt.content, placeholder = opt.placeholder,
                   cursor_pos](InputState state) {
    return TransformBlinkInput(state, *content, *placeholder, cursor_pos());
  };
  return opt;
}

}  // namespace tgdb
