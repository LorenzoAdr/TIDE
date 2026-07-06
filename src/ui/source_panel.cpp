#include "ui/source_panel.hpp"

#include <chrono>
#include <fstream>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>

#include "backend/idebug_backend.hpp"
#include "editor/text_search.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/focusable_component.hpp"
#include "ui/main_layout.hpp"
#include "ui/clickable.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/cpp_highlight.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct SourcePanelState {
  Box content_box;
  Box gutter_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  int last_visible_lines = 1;
  uint64_t last_view_token = 0;
};

constexpr int kDebugHoverDelayMs = 500;

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void assign_evaluate_frame(UiCommand* command, const DebugModel* model) {
  if (command == nullptr || model == nullptr) {
    return;
  }
  if (model->variables_frame_id >= 0) {
    command->frame_id = model->variables_frame_id;
  } else if (!model->stack_frames.empty()) {
    command->frame_id = model->stack_frames[model->selected_frame].id;
  }
}

std::optional<std::string> lookup_local_value(const DebugModel& model, const std::string& word) {
  if (word.empty()) {
    return std::nullopt;
  }
  for (const VariableInfo& local : model.locals) {
    if (local.name == word) {
      return local.value;
    }
  }
  return std::nullopt;
}

void track_source_debug_hover(DebugModel* model, SourceViewState* view_state,
                              const SourcePanelState& panel, const Mouse& m, int visible_lines) {
  if (model == nullptr || view_state == nullptr || model->state != DebugState::kStopped) {
    return;
  }
  const int total = static_cast<int>(view_state->lines.size());
  if (total <= 0) {
    return;
  }
  const int rel_y = m.y - panel.content_box.y_min;
  const int line = std::max(0, std::min(view_state->scroll + rel_y, total - 1));
  const int col = std::max(0, m.x - panel.content_box.x_min);
  auto& hover = view_state->debug_hover;
  if (line == hover.line && col == hover.col) {
    return;
  }
  hover.line = line;
  hover.col = col;
  hover.anchor_x = m.x;
  hover.anchor_y = m.y;
  hover.dwell_start_ms = steady_now_ms();
  hover.visible = false;
  hover.fetch_key.clear();
  hover.pending_expression.clear();
  hover.waiting_evaluate = false;
  hover.title.clear();
  hover.body_lines.clear();
}

void source_debug_hover_tick(DebugModel* model, SourceViewState* view_state,
                             CommandCallback on_command) {
  if (model == nullptr || view_state == nullptr) {
    return;
  }
  auto& hover = view_state->debug_hover;
  if (model->state != DebugState::kStopped) {
    if (hover.line >= 0 || hover.visible) {
      clear_source_debug_hover(&hover);
    }
    return;
  }
  if (hover.line < 0 || hover.visible) {
    return;
  }
  const int64_t now_ms = steady_now_ms();
  if (now_ms - hover.dwell_start_ms < kDebugHoverDelayMs) {
    return;
  }
  if (hover.line < 0 || hover.line >= static_cast<int>(view_state->lines.size())) {
    return;
  }

  const std::string key = model->active_file + "|" + std::to_string(hover.line) + "|" +
                          std::to_string(hover.col) + "|" + std::to_string(model->view_token);
  if (hover.fetch_key == key) {
    if (hover.waiting_evaluate) {
      return;
    }
    hover.visible = !hover.title.empty() || !hover.body_lines.empty();
    return;
  }

  const std::string& line_text = view_state->lines[static_cast<std::size_t>(hover.line)];
  const std::string word = word_at_line_col(line_text, hover.col);
  if (word.empty()) {
    return;
  }

  hover.fetch_key = key;
  hover.pending_expression = word;
  hover.title = word;
  hover.body_lines.clear();

  if (const auto local_value = lookup_local_value(*model, word); local_value.has_value()) {
    hover.body_lines.push_back(*local_value);
    hover.visible = true;
    hover.waiting_evaluate = false;
    return;
  }

  if (!on_command) {
    return;
  }
  UiCommand command;
  command.kind = UiCommandKind::kEvaluate;
  command.expression = word;
  command.evaluate_context = EvaluateContext::kHover;
  command.correlation_id = key;
  assign_evaluate_frame(&command, model);
  on_command(command);
  hover.waiting_evaluate = true;
}

Element make_source_hover_tooltip(const SourceDebugHoverState& hover, const Box& code_box) {
  if (!hover.visible || hover.title.empty()) {
    return text("");
  }

  Elements rows;
  rows.push_back(text(" " + hover.title) | bold | color(theme::Accent()) | bgcolor(theme::PanelBg()));
  for (const std::string& line : hover.body_lines) {
    rows.push_back(text(" " + line) | color(theme::Header()) | bgcolor(theme::PanelBg()));
  }
  if (rows.size() <= 1 && hover.waiting_evaluate) {
    rows.push_back(text(" …") | color(theme::Muted()) | bgcolor(theme::PanelBg()));
  }

  const int popup_rows = static_cast<int>(rows.size());
  Element popup = vbox(std::move(rows)) | border | bgcolor(theme::PanelBg());
  const int rel_x = std::max(0, hover.anchor_x - code_box.x_min + 1);
  const int rel_y = std::max(0, hover.anchor_y - code_box.y_min + 1);
  const int code_h = std::max(1, code_box.y_max - code_box.y_min + 1);
  const bool place_above = rel_y + popup_rows + 2 >= code_h;
  const int y_pad = place_above ? std::max(0, rel_y - popup_rows - 1) : rel_y + 1;

  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, rel_x), popup | clear_under, filler()}),
                     filler()}) |
                   flex});
}

void load_source_file(const std::string& path, std::vector<std::string>* lines) {
  lines->clear();
  if (path.empty()) {
    lines->push_back(i18n::tr("panel.source.empty_hint"));
    return;
  }

  std::ifstream input(path);
  if (!input) {
    lines->push_back(i18n::tr_fmt("panel.source.open_failed", {path}));
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

bool handle_source_scrollbar_mouse(SourceViewState* view_state, SourcePanelState* panel,
                                   FocusManagerState* focus, MainLayoutState* layout_state,
                                   const Mouse& m, int total, int visible) {
  if (view_state == nullptr || panel == nullptr || !panel->scrollbar_layout.scrollable) {
    return false;
  }

  const int max_scroll = max_scroll_offset(total, visible);
  const bool in_bar = panel->scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || panel->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kSourceScrollbar);
      } else {
        layout_state->clickable.clear_hover_if([&](std::string_view id) {
          return id == press_id::kSourceScrollbar;
        });
      }
      if (layout_state->clickable.hovered_id() != before) {
        layout_state->request_ui_tick = true;
      }
    }
    if (panel->scrollbar_dragging) {
      const int local_y = m.y - panel->scrollbar_box.y_min;
      const int thumb_top = local_y - panel->scrollbar_drag_offset;
      view_state->scroll =
          std::max(0, std::min(scroll_for_thumb_top(panel->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
    return in_bar;
  }

  if (panel->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      panel->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      const int local_y = m.y - panel->scrollbar_box.y_min;
      const int thumb_top = local_y - panel->scrollbar_drag_offset;
      view_state->scroll =
          std::max(0, std::min(scroll_for_thumb_top(panel->scrollbar_layout, thumb_top),
                               max_scroll));
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    view_state->scroll = std::max(0, view_state->scroll - 3);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    view_state->scroll = std::min(view_state->scroll + 3, max_scroll);
    return true;
  }

  if (m.button != Mouse::Left) {
    return false;
  }

  if (m.motion == Mouse::Pressed) {
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
      layout_state->right_panel_active_section = 0;
      layout_state->focus_sync_needed = true;
    }
    trigger_press(layout_state, press_id::kSourceScrollbar);
    const int local_y = m.y - panel->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(panel->scrollbar_layout, panel->scrollbar_box, m.x, m.y)) {
      panel->scrollbar_dragging = true;
      panel->scrollbar_drag_offset = local_y - panel->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - panel->scrollbar_layout.thumb_height / 2;
      view_state->scroll =
          std::max(0, std::min(scroll_for_thumb_top(panel->scrollbar_layout, thumb_top),
                               max_scroll));
      panel->scrollbar_dragging = true;
      panel->scrollbar_drag_offset = panel->scrollbar_layout.thumb_height / 2;
    }
    return true;
  }

  if (m.motion == Mouse::Moved && panel->scrollbar_dragging) {
    const int local_y = m.y - panel->scrollbar_box.y_min;
    const int thumb_top = local_y - panel->scrollbar_drag_offset;
    view_state->scroll =
        std::max(0, std::min(scroll_for_thumb_top(panel->scrollbar_layout, thumb_top),
                             max_scroll));
    return true;
  }

  return false;
}

bool source_panel_contains_mouse(const SourcePanelState& panel, const Mouse& m) {
  return panel.gutter_box.Contain(m.x, m.y) || panel.content_box.Contain(m.x, m.y) ||
         panel.scrollbar_box.Contain(m.x, m.y);
}

bool handle_source_panel_event(DebugModel* model, SourceViewState* view_state,
                               SourcePanelState* panel_state, CommandCallback on_command,
                               FocusManagerState* focus, MainLayoutState* layout_state,
                               Event event) {
  if (model == nullptr || view_state == nullptr || panel_state == nullptr) {
    return false;
  }

  const int total = static_cast<int>(view_state->lines.size());
  const int visible = panel_state->last_visible_lines;
  const int max_scroll = max_scroll_offset(total, visible);

  if (event.is_mouse()) {
    const auto& m = event.mouse();
    if (handle_source_scrollbar_mouse(view_state, panel_state, focus, layout_state, m, total,
                                      visible)) {
      return true;
    }
    if (m.motion == Mouse::Moved) {
      if (panel_state->content_box.Contain(m.x, m.y)) {
        track_source_debug_hover(model, view_state, *panel_state, m, visible);
      } else if (!panel_state->content_box.Contain(m.x, m.y)) {
        clear_source_debug_hover(&view_state->debug_hover);
      }
    }
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
    clear_source_debug_hover(&view_state->debug_hover);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    view_state->scroll = std::min(view_state->scroll + 1, max_scroll);
    clear_source_debug_hover(&view_state->debug_hover);
    return true;
  }
  if (event == Event::PageUp) {
    view_state->scroll = std::max(0, view_state->scroll - visible);
    clear_source_debug_hover(&view_state->debug_hover);
    return true;
  }
  if (event == Event::PageDown) {
    view_state->scroll = std::min(view_state->scroll + visible, max_scroll);
    clear_source_debug_hover(&view_state->debug_hover);
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
}

}  // namespace

void clear_source_debug_hover(SourceDebugHoverState* hover) {
  if (hover == nullptr) {
    return;
  }
  hover->line = -1;
  hover->col = -1;
  hover->anchor_x = 0;
  hover->anchor_y = 0;
  hover->dwell_start_ms = 0;
  hover->fetch_key.clear();
  hover->pending_expression.clear();
  hover->waiting_evaluate = false;
  hover->title.clear();
  hover->body_lines.clear();
  hover->visible = false;
}

void ToggleBreakpointAtFile(DebugModel* model, const std::string& file, int line,
                            CommandCallback on_command) {
  if (model == nullptr || file.empty() || line <= 0) {
    return;
  }
  const std::string normalized = normalize_path(file);
  model->toggle_breakpoint(normalized, line);

  if (!on_command) {
    return;
  }
  UiCommand command;
  command.kind = UiCommandKind::kSetBreakpoints;
  command.breakpoint_file = normalized;
  command.breakpoint_lines = model->enabled_breakpoint_lines(normalized);
  on_command(command);
}

void ToggleBreakpointAtLine(DebugModel* model, int line, CommandCallback on_command) {
  if (model == nullptr || model->active_file.empty() || line <= 0) {
    return;
  }
  ToggleBreakpointAtFile(model, model->active_file, line, on_command);
}

Component MakeSourcePanel(DebugModel* model, SourceViewState* view_state,
                          CommandCallback on_command, FocusManagerState* focus,
                          MainLayoutState* layout_state) {
  auto loaded_file = std::make_shared<std::string>();
  auto panel_state = std::make_shared<SourcePanelState>();

  auto renderer = Renderer([model, view_state, loaded_file, panel_state, layout_state] {
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
      code_rows.push_back(text(i18n::tr("panel.source.no_lines")) | dim);
    }

    const int rendered_lines = static_cast<int>(code_rows.size());

    std::string title = i18n::tr("panel.source.title");
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
        vertical_scrollbar(total, view_state->scroll, visible, rendered_lines,
                           layout_state != nullptr &&
                               layout_state->clickable.is_hovered(press_id::kSourceScrollbar),
                           panel_state->scrollbar_dragging ||
                               (layout_state != nullptr &&
                                layout_state->clickable.is_pressed(press_id::kSourceScrollbar))) |
        reflect(panel_state->scrollbar_box);
    panel_state->scrollbar_layout =
        compute_scrollbar_layout(total, view_state->scroll, visible, rendered_lines);

    Element panel = MakePanel(title, hbox({gutter, separator() | color(theme::AccentDim()), code | flex,
                                           scrollbar}),
                              theme::CodeBg());
    Element overlay = make_source_hover_tooltip(view_state->debug_hover, panel_state->content_box);
    return dbox({std::move(panel), std::move(overlay)}) | flex;
  });

  auto dispatch_source_mouse = [model, view_state, on_command, panel_state, focus,
                                layout_state](Event event) {
    if (!event.is_mouse()) {
      return false;
    }
    const Mouse& m = event.mouse();
    if (m.motion == Mouse::Moved) {
      handle_source_scrollbar_mouse(view_state, panel_state.get(), focus, layout_state, m,
                                    static_cast<int>(view_state->lines.size()),
                                    panel_state->last_visible_lines);
      if (!source_panel_contains_mouse(*panel_state, m) && !panel_state->scrollbar_dragging) {
        clear_source_debug_hover(&view_state->debug_hover);
        return layout_state != nullptr && layout_state->request_ui_tick;
      }
    } else if (!source_panel_contains_mouse(*panel_state, m) && !panel_state->scrollbar_dragging) {
      return false;
    }
    return handle_source_panel_event(model, view_state, panel_state.get(), on_command, focus,
                                     layout_state, event);
  };

  auto dispatch_source_keys = [model, view_state, on_command, panel_state, focus,
                               layout_state](Event event) {
    if (event.is_mouse()) {
      return false;
    }
    return handle_source_panel_event(model, view_state, panel_state.get(), on_command, focus,
                                     layout_state, event);
  };

  if (layout_state != nullptr) {
    layout_state->source_mouse_handler = dispatch_source_mouse;
    layout_state->source_key_handler = dispatch_source_keys;
    layout_state->source_tick_callback = [model, view_state, on_command]() {
      source_debug_hover_tick(model, view_state, on_command);
    };
  }

  return WrapFocusable(CatchEvent(renderer, [model, view_state, on_command, panel_state, focus,
                                               layout_state](Event event) {
    return handle_source_panel_event(model, view_state, panel_state.get(), on_command, focus,
                                     layout_state, event);
  }));
}

}  // namespace tgdb
