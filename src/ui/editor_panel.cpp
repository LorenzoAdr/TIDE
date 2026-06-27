#include "ui/editor_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "util/cpp_highlight.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct EditorPanelState {
  Box code_box;
  Box gutter_box;
  uint64_t last_view_token = 0;
};

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll(int total, int visible) {
  return std::max(0, total - visible);
}

void clamp_cursor(EditorBuffer* buffer) {
  if (buffer->lines.empty()) {
    buffer->lines.push_back("");
  }
  buffer->cursor_line =
      std::max(0, std::min(buffer->cursor_line, static_cast<int>(buffer->lines.size()) - 1));
  const int line_len = static_cast<int>(buffer->lines[buffer->cursor_line].size());
  buffer->cursor_col = std::max(0, std::min(buffer->cursor_col, line_len));
}

void ensure_scroll_visible(EditorBuffer* buffer, int visible_lines) {
  if (buffer->cursor_line < buffer->scroll) {
    buffer->scroll = buffer->cursor_line;
  } else if (buffer->cursor_line >= buffer->scroll + visible_lines) {
    buffer->scroll = std::max(0, buffer->cursor_line - visible_lines + 1);
  }
  buffer->scroll = std::max(
      0, std::min(buffer->scroll, max_scroll(static_cast<int>(buffer->lines.size()), visible_lines)));
}

void insert_char(EditorBuffer* buffer, char c) {
  clamp_cursor(buffer);
  auto& line = buffer->lines[buffer->cursor_line];
  line.insert(static_cast<std::size_t>(buffer->cursor_col), 1, c);
  ++buffer->cursor_col;
  buffer->dirty = true;
}

void backspace(EditorBuffer* buffer) {
  clamp_cursor(buffer);
  if (buffer->cursor_col > 0) {
    buffer->lines[buffer->cursor_line].erase(
        static_cast<std::size_t>(buffer->cursor_col - 1), 1);
    --buffer->cursor_col;
    buffer->dirty = true;
    return;
  }
  if (buffer->cursor_line > 0) {
    const std::string tail = buffer->lines[buffer->cursor_line];
    buffer->lines.erase(buffer->lines.begin() + buffer->cursor_line);
    --buffer->cursor_line;
    buffer->cursor_col = static_cast<int>(buffer->lines[buffer->cursor_line].size());
    buffer->lines[buffer->cursor_line] += tail;
    buffer->dirty = true;
  }
}

void delete_char(EditorBuffer* buffer) {
  clamp_cursor(buffer);
  auto& line = buffer->lines[buffer->cursor_line];
  if (buffer->cursor_col < static_cast<int>(line.size())) {
    line.erase(static_cast<std::size_t>(buffer->cursor_col), 1);
    buffer->dirty = true;
    return;
  }
  if (buffer->cursor_line + 1 < static_cast<int>(buffer->lines.size())) {
    line += buffer->lines[buffer->cursor_line + 1];
    buffer->lines.erase(buffer->lines.begin() + buffer->cursor_line + 1);
    buffer->dirty = true;
  }
}

void newline(EditorBuffer* buffer) {
  clamp_cursor(buffer);
  auto& line = buffer->lines[buffer->cursor_line];
  const std::string tail = line.substr(static_cast<std::size_t>(buffer->cursor_col));
  line.erase(static_cast<std::size_t>(buffer->cursor_col));
  buffer->lines.insert(buffer->lines.begin() + buffer->cursor_line + 1, tail);
  ++buffer->cursor_line;
  buffer->cursor_col = 0;
  buffer->dirty = true;
}

int line_number_width(int total_lines) {
  const int digits = std::max(1, static_cast<int>(std::to_string(total_lines).size()));
  return digits + 1;
}

std::string format_line_number(int line_no, int width) {
  std::string text = std::to_string(line_no);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

Element vertical_scrollbar(int total_lines, int scroll, int visible_lines, int bar_height) {
  Elements track;
  if (bar_height <= 0) {
    return text("");
  }

  if (total_lines <= visible_lines) {
    for (int i = 0; i < bar_height; ++i) {
      track.push_back(text("│") | color(theme::Muted()));
    }
    return vbox(std::move(track));
  }

  const int thumb_height = std::max(1, visible_lines * bar_height / total_lines);
  const int max_scroll_pos = total_lines - visible_lines;
  const int thumb_y = max_scroll_pos > 0
                          ? (scroll * (bar_height - thumb_height)) / max_scroll_pos
                          : 0;

  for (int i = 0; i < bar_height; ++i) {
    if (i >= thumb_y && i < thumb_y + thumb_height) {
      track.push_back(text("┃") | color(theme::Accent()));
    } else {
      track.push_back(text("│") | color(theme::Muted()));
    }
  }
  return vbox(std::move(track));
}

Element render_editor_line(const std::string& line, int cursor_col, bool is_current_line,
                           bool show_cursor) {
  const Decorator line_bg =
      is_current_line ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());
  const Decorator cursor_cell =
      bgcolor(theme::CursorCell()) | color(Color::Black) | bold;

  if (!show_cursor) {
    Element content = line.empty() ? text(" ") : HighlightCppLine(line);
    return content | line_bg;
  }

  cursor_col = std::max(0, std::min(cursor_col, static_cast<int>(line.size())));

  if (line.empty() || cursor_col >= static_cast<int>(line.size())) {
    Elements parts;
    if (!line.empty()) {
      parts.push_back(HighlightCppLine(line));
    }
    parts.push_back(text(" ") | cursor_cell);
    return hbox(std::move(parts)) | line_bg;
  }

  return HighlightCppLine(line, cursor_col, cursor_cell) | line_bg;
}

bool handle_editor_mouse(EditorBuffer* buffer, FocusManagerState* focus,
                         const EditorPanelState& panel, Event event, int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }

  const auto& m = event.mouse();
  const bool in_code = panel.code_box.Contain(m.x, m.y);
  const bool in_gutter = panel.gutter_box.Contain(m.x, m.y);
  if (!in_code && !in_gutter) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    buffer->scroll = std::max(0, buffer->scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    buffer->scroll = std::min(
        max_scroll(static_cast<int>(buffer->lines.size()), visible_lines),
        buffer->scroll + 3);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    focus->region = FocusRegion::Editor;
    const int row = m.y - (in_gutter ? panel.gutter_box.y_min : panel.code_box.y_min);
    buffer->cursor_line =
        std::max(0, std::min(buffer->scroll + row,
                             static_cast<int>(buffer->lines.size()) - 1));
    if (in_code) {
      const int col = std::max(0, m.x - panel.code_box.x_min);
      const int line_len = static_cast<int>(buffer->lines[buffer->cursor_line].size());
      buffer->cursor_col = std::min(col, line_len);
    }
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }

  return false;
}

bool handle_editor_keys(WorkspaceModel* workspace, FocusManagerState* focus, Event event,
                        int visible_lines) {
  if (focus->region != FocusRegion::Editor) {
    return false;
  }
  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;

  if (event == Event::CtrlS) {
    workspace->save_buffer();
    return true;
  }
  if (event == Event::ArrowLeft) {
    if (buffer->cursor_col > 0) {
      --buffer->cursor_col;
    } else if (buffer->cursor_line > 0) {
      --buffer->cursor_line;
      buffer->cursor_col = static_cast<int>(buffer->lines[buffer->cursor_line].size());
    }
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowRight) {
    const int len = static_cast<int>(buffer->lines[buffer->cursor_line].size());
    if (buffer->cursor_col < len) {
      ++buffer->cursor_col;
    } else if (buffer->cursor_line + 1 < static_cast<int>(buffer->lines.size())) {
      ++buffer->cursor_line;
      buffer->cursor_col = 0;
    }
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowUp) {
    if (buffer->cursor_line > 0) {
      --buffer->cursor_line;
      clamp_cursor(buffer);
    }
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowDown) {
    if (buffer->cursor_line + 1 < static_cast<int>(buffer->lines.size())) {
      ++buffer->cursor_line;
      clamp_cursor(buffer);
    }
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Home) {
    buffer->cursor_col = 0;
    return true;
  }
  if (event == Event::End) {
    buffer->cursor_col = static_cast<int>(buffer->lines[buffer->cursor_line].size());
    return true;
  }
  if (event == Event::Backspace) {
    backspace(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Delete) {
    delete_char(buffer);
    return true;
  }
  if (event == Event::Return) {
    newline(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Tab) {
    insert_char(buffer, '\t');
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::PageDown) {
    buffer->cursor_line = std::min(
        buffer->cursor_line + visible_lines,
        static_cast<int>(buffer->lines.size()) - 1);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::PageUp) {
    buffer->cursor_line = std::max(0, buffer->cursor_line - visible_lines);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      insert_char(buffer, ch[0]);
      ensure_scroll_visible(buffer, visible_lines);
      return true;
    }
  }
  return false;
}

}  // namespace

Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus) {
  auto state = std::make_shared<EditorPanelState>();

  auto renderer = Renderer([workspace, focus, state] {
    workspace->ensure_buffer();
    EditorBuffer& buffer = workspace->buffer;

    if (buffer.view_token != state->last_view_token) {
      state->last_view_token = buffer.view_token;
      buffer.scroll = std::max(0, buffer.cursor_line - 2);
    }

    const int visible = visible_line_count(state->code_box);
    ensure_scroll_visible(&buffer, visible);

    const int total = static_cast<int>(buffer.lines.size());
    const int start = buffer.scroll;
    const int end = std::min(total, start + visible);
    const int gutter_w = line_number_width(total);

    Elements gutter_rows;
    Elements code_rows;
    for (int i = start; i < end; ++i) {
      const bool is_current = (i == buffer.cursor_line);
      const bool show_cursor = is_current && focus->region == FocusRegion::Editor;
      const Decorator row_bg =
          is_current ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());

      gutter_rows.push_back(text(format_line_number(i + 1, gutter_w)) | color(theme::Muted()) |
                            row_bg);

      code_rows.push_back(
          render_editor_line(buffer.lines[i], buffer.cursor_col, is_current, show_cursor));
    }
    if (code_rows.empty()) {
      gutter_rows.push_back(text(format_line_number(1, gutter_w)) | color(theme::Muted()) |
                            bgcolor(theme::CodeBg()));
      code_rows.push_back(text(" ") | bgcolor(theme::CodeBg()));
    }

    const int rendered_lines = static_cast<int>(code_rows.size());

    std::string title = "Editor";
    if (!buffer.path.empty()) {
      title = std::filesystem::path(buffer.path).filename().string();
      if (buffer.dirty) {
        title += " *";
      }
    }
    title += "  L" + std::to_string(buffer.cursor_line + 1) + ":" +
             std::to_string(buffer.cursor_col + 1);

    Element gutter =
        vbox(std::move(gutter_rows)) | reflect(state->gutter_box) | bgcolor(theme::CodeBg());
    Element code = vbox(std::move(code_rows)) | flex | reflect(state->code_box) |
                   bgcolor(theme::CodeBg());
    Element scrollbar = vertical_scrollbar(total, buffer.scroll, visible, rendered_lines);

    auto content =
        hbox({gutter, separator() | color(theme::AccentDim()), code | flex, scrollbar}) | frame |
        flex | bgcolor(theme::CodeBg());
    return MakePanel(title, std::move(content), theme::CodeBg());
  });

  return WrapFocusable(CatchEvent(renderer, [workspace, focus, state](Event event) {
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    const int visible = visible_line_count(state->code_box);

    if (handle_editor_mouse(buffer, focus, *state, event, visible)) {
      return true;
    }

    return handle_editor_keys(workspace, focus, event, visible);
  }));
}

}  // namespace tgdb
