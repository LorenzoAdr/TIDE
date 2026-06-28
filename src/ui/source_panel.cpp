#include "ui/source_panel.hpp"

#include <fstream>
#include <filesystem>
#include <memory>
#include <sstream>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/focusable_component.hpp"
#include "ui/main_layout.hpp"
#include "ui/theme.hpp"
#include "util/cpp_highlight.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct SourcePanelState {
  Box content_box;
  Box gutter_box;
  int last_visible_lines = 1;
  uint64_t last_view_token = 0;
};

void load_source_file(const std::string& path, std::vector<std::string>* lines) {
  lines->clear();
  if (path.empty()) {
    lines->push_back("Abre un archivo o inicia una sesión de depuración.");
    return;
  }

  std::ifstream input(path);
  if (!input) {
    lines->push_back("No se pudo abrir: " + path);
    return;
  }

  std::string line;
  while (std::getline(input, line)) {
    lines->push_back(line);
  }
  if (lines->empty()) {
    lines->push_back("");
  }
}

int line_number_width(int total_lines) {
  const int digits = std::max(1, static_cast<int>(std::to_string(total_lines).size()));
  return digits + 2;
}

std::string format_line_number(int line_no, int width) {
  std::string text = std::to_string(line_no);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll_offset(int total_lines, int visible_lines) {
  return std::max(0, total_lines - visible_lines);
}

void clamp_scroll(SourceViewState* view_state, int total_lines, int visible_lines) {
  view_state->scroll = std::max(
      0, std::min(view_state->scroll, max_scroll_offset(total_lines, visible_lines)));
}

void scroll_to_line(SourceViewState* view_state, int line, int visible_lines) {
  if (line <= 0 || visible_lines <= 0) {
    return;
  }
  const int index = line - 1;
  if (index < view_state->scroll) {
    view_state->scroll = std::max(0, index);
  } else if (index >= view_state->scroll + visible_lines) {
    view_state->scroll = std::max(0, index - visible_lines + 1);
  }
}

Element vertical_scrollbar(int total_lines, int scroll, int visible_lines,
                           int bar_height) {
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

  const int thumb_height =
      std::max(1, visible_lines * bar_height / total_lines);
  const int max_scroll = total_lines - visible_lines;
  const int thumb_y =
      max_scroll > 0 ? (scroll * (bar_height - thumb_height)) / max_scroll : 0;

  for (int i = 0; i < bar_height; ++i) {
    if (i >= thumb_y && i < thumb_y + thumb_height) {
      track.push_back(text("┃") | color(theme::Accent()));
    } else {
      track.push_back(text("│") | color(theme::Muted()));
    }
  }
  return vbox(std::move(track));
}

}  // namespace

void ToggleBreakpointAtLine(DebugModel* model, int line, CommandCallback on_command) {
  if (!on_command || model->active_file.empty() || line <= 0) {
    return;
  }
  model->active_file = normalize_path(model->active_file);

  model->toggle_breakpoint(model->active_file, line);

  UiCommand command;
  command.kind = UiCommandKind::kSetBreakpoints;
  command.breakpoint_file = model->active_file;
  command.breakpoint_lines = model->enabled_breakpoint_lines(model->active_file);
  on_command(command);
}

Component MakeSourcePanel(DebugModel* model, SourceViewState* view_state,
                          CommandCallback on_command, FocusManagerState* focus,
                          MainLayoutState* layout_state) {
  auto loaded_file = std::make_shared<std::string>();
  auto panel_state = std::make_shared<SourcePanelState>();

  auto renderer = Renderer([model, view_state, loaded_file, panel_state] {
    if (*loaded_file != model->active_file) {
      *loaded_file = model->active_file;
      load_source_file(*loaded_file, &view_state->lines);
    }

    const int total = static_cast<int>(view_state->lines.size());
    const int visible = visible_line_count(panel_state->content_box);
    panel_state->last_visible_lines = visible;

    if (model->view_token != panel_state->last_view_token) {
      panel_state->last_view_token = model->view_token;
      scroll_to_line(view_state, model->active_line, visible);
    }

    clamp_scroll(view_state, total, visible);

    const int start = view_state->scroll;
    const int end = std::min(total, start + visible);

    const int gutter_w = line_number_width(total);

    Elements gutter_rows;
    Elements code_rows;
    for (int i = start; i < end; ++i) {
      const int line_no = i + 1;
      const bool is_current = line_no == model->active_line;
      const bool is_bp = model->has_breakpoint(model->active_file, line_no);

      std::string marker = " ";
      if (is_current) {
        marker = "►";
      } else if (is_bp) {
        marker = "●";
      }

      Element marker_el = text(marker);
      if (is_current) {
        marker_el = marker_el | bold;
      } else if (is_bp) {
        marker_el = marker_el | color(Color::Red);
      }

      Element gutter_row =
          hbox({marker_el, text(format_line_number(line_no, gutter_w))}) | color(theme::Muted());
      if (is_current) {
        gutter_row = gutter_row | inverted;
      }
      gutter_rows.push_back(gutter_row);

      Element code_row = HighlightCppLine(view_state->lines[i]);
      if (is_current) {
        code_row = code_row | inverted;
      } else if (is_bp) {
        code_row = code_row | color(Color::Red);
      }
      code_rows.push_back(code_row);
    }

    if (gutter_rows.empty()) {
      gutter_rows.push_back(text(" ") | dim);
      code_rows.push_back(text("(sin líneas)") | dim);
    }

    const int rendered_lines = static_cast<int>(code_rows.size());

    std::string title = "Código";
    if (!model->active_file.empty()) {
      const auto filename =
          std::filesystem::path(model->active_file).filename().string();
      title = filename;
    }

    Element gutter =
        vbox(std::move(gutter_rows)) | reflect(panel_state->gutter_box) | bgcolor(theme::CodeBg());
    Element code = vbox(std::move(code_rows)) | flex | reflect(panel_state->content_box) |
                   bgcolor(theme::CodeBg());
    Element scrollbar =
        vertical_scrollbar(total, view_state->scroll, visible, rendered_lines);

    return MakePanel(title, hbox({gutter, separator() | color(theme::AccentDim()), code | flex,
                                  scrollbar}),
                     theme::CodeBg());
  });

  return WrapFocusable(CatchEvent(renderer, [model, view_state, on_command, panel_state, focus,
                                               layout_state](Event event) {
    if (event.is_mouse()) {
      const auto& m = event.mouse();
      if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
        const bool in_gutter = panel_state->gutter_box.Contain(m.x, m.y);
        const bool in_code = panel_state->content_box.Contain(m.x, m.y);
        if (in_gutter || in_code) {
          if (focus != nullptr) {
            focus->region = FocusRegion::Editor;
          }
          if (layout_state != nullptr) {
            layout_state->text_input_focus = TextInputFocus::None;
            layout_state->right_panel_active_section = 0;
            layout_state->focus_sync_needed = true;
          }
        }
      }
    } else if (focus != nullptr && focus->region == FocusRegion::RightPanel) {
      return false;
    }

    const int total = static_cast<int>(view_state->lines.size());
    const int visible = panel_state->last_visible_lines;
    const int max_scroll = max_scroll_offset(total, visible);

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (panel_state->gutter_box.Contain(m.x, m.y)) {
        const int rel_y = m.y - panel_state->gutter_box.y_min;
        const int clicked_line = view_state->scroll + rel_y + 1;
        if (clicked_line >= 1 && clicked_line <= total && !model->active_file.empty()) {
          ToggleBreakpointAtLine(model, clicked_line, on_command);
          return true;
        }
      }
      if (panel_state->content_box.Contain(m.x, m.y)) {
        const int rel_y = m.y - panel_state->content_box.y_min;
        const int clicked_line = view_state->scroll + rel_y + 1;
        if (clicked_line >= 1 && clicked_line <= total) {
          model->active_line = clicked_line;
          model->view_token++;
          return true;
        }
      }
      return false;
    }

    if (event == Event::CtrlB || event == Event::Character(' ')) {
      if (!model->active_file.empty() && model->active_line > 0) {
        ToggleBreakpointAtLine(model, model->active_line, on_command);
        return true;
      }
      return false;
    }

    if (event == Event::ArrowUp || event == Event::Character('k')) {
      view_state->scroll = std::max(0, view_state->scroll - 1);
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      view_state->scroll = std::min(view_state->scroll + 1, max_scroll);
      return true;
    }
    if (event == Event::PageUp) {
      view_state->scroll = std::max(0, view_state->scroll - visible);
      return true;
    }
    if (event == Event::PageDown) {
      view_state->scroll = std::min(view_state->scroll + visible, max_scroll);
      return true;
    }
    const int half_page = std::max(1, visible / 2);
    if (event_is_ctrl_u(event)) {
      view_state->scroll = std::max(0, view_state->scroll - half_page);
      return true;
    }
    if (event_is_ctrl_i(event)) {
      view_state->scroll = std::min(view_state->scroll + half_page, max_scroll);
      return true;
    }
    if (event.is_mouse() && event.mouse().motion == Mouse::Pressed) {
      if (event.mouse().button == Mouse::WheelUp) {
        view_state->scroll = std::max(0, view_state->scroll - 3);
        return true;
      }
      if (event.mouse().button == Mouse::WheelDown) {
        view_state->scroll = std::min(view_state->scroll + 3, max_scroll);
        return true;
      }
    }

    return false;
  }));
}

}  // namespace tgdb
