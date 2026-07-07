#include "ui/console_panel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <vector>

#include "backend/idebug_backend.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "terminal/pty_input.hpp"
#include "ui/focus_manager.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/cursor_blink_ui.hpp"
#include "ui/press_ids.hpp"
#include "ui/panel.hpp"
#include "app/workspace_model.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/call_hierarchy_panel.hpp"
#include "ui/binary_symbols_panel.hpp"
#include "ui/core_analyzer_panel.hpp"
#include "ui/diagnostics_panel.hpp"
#include "ui/git_panel.hpp"
#include "ui/search_panel.hpp"
#include "ui/performance_panel.hpp"
#include "ui/packet_monitor_panel.hpp"
#include "ui/text_input_style.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/terminal_display.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/compiler_location.hpp"
#include "util/monitor_log.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct ConsolePanelState {
  std::string input;
  std::string input_placeholder;
  Box panel_box;
  Box content_box;
  Box history_box;
  Box terminal_box;
  Box terminal_scrollbar_box;
  ScrollbarLayout terminal_scrollbar_layout;
  bool terminal_scrollbar_dragging = false;
  int terminal_scrollbar_drag_offset = 0;
  int terminal_first_visible = 0;
  int terminal_last_visible_lines = 1;
  bool terminal_follow_tail = true;
  Box input_box;
  int last_visible_lines = 1;
  int first_visible = 0;
  std::size_t last_output_size = 0;
  bool follow_tail = true;
  int last_terminal_cols = 0;
  int last_terminal_rows = 0;
  int pending_terminal_cols = 80;
  int pending_terminal_rows = 24;
  int git_body_height = 8;
  int applied_terminal_cols = 0;
  int applied_terminal_rows = 0;
  bool layout_measured = false;
  bool shell_start_requested = false;
  bool shell_start_failed = false;
  bool shell_launch_uses_docker = false;
  std::string shell_docker_container;
  bool terminal_resize_applied = false;
  bool terminal_view_valid = false;
  bool shell_ui_active = false;
  std::string terminal_text;
  std::vector<TerminalStyledRow> terminal_styled_rows;
  struct TerminalLinkHover {
    int row = -1;
    int span_start = 0;
    int span_end = 0;

    bool operator==(const TerminalLinkHover& other) const {
      return row == other.row && span_start == other.span_start && span_end == other.span_end;
    }
  };
  std::optional<TerminalLinkHover> terminal_link_hover;
  std::string last_workspace_root;
  std::array<Box, 10> tab_boxes;
  Box hide_box;
};

void handle_gdb_command(const std::string& line, DebugModel* model, CommandCallback on_command) {
  if (line.empty() || !on_command) {
    return;
  }

  if (line.rfind("watch ", 0) == 0) {
    const std::string expression = line.substr(6);
    model->add_watch(expression);
    UiCommand command;
    command.kind = UiCommandKind::kAddWatch;
    command.expression = expression;
    if (!model->stack_frames.empty()) {
      if (model->variables_frame_id >= 0) {
        command.frame_id = model->variables_frame_id;
      } else {
        command.frame_id = model->stack_frames[model->selected_frame].id;
      }
    }
    on_command(command);
    return;
  }

  UiCommand command;
  command.kind = UiCommandKind::kEvaluate;
  command.expression = line;
  command.evaluate_context = EvaluateContext::kRepl;
  if (!model->stack_frames.empty()) {
    if (model->variables_frame_id >= 0) {
      command.frame_id = model->variables_frame_id;
    } else {
      command.frame_id = model->stack_frames[model->selected_frame].id;
    }
  }
  on_command(command);
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int visible_column_count(const Box& box) {
  if (box.x_max < box.x_min) {
    return 1;
  }
  return std::max(1, box.x_max - box.x_min + 1);
}

int max_first_visible(int total_lines, int visible_lines) {
  return std::max(0, total_lines - visible_lines);
}

bool terminal_box_valid(const Box& box);
bool terminal_body_contains(const ConsolePanelState* state, int x, int y);
int measure_terminal_cols(const ConsolePanelState* state, int panel_width);
int measure_terminal_rows(int viewport_height);
void update_terminal_layout(ConsolePanelState* state, int panel_height, int panel_width,
                            MainLayoutState* layout_state);

std::string terminal_row_text(const TerminalStyledRow& row) {
  std::string text;
  for (const TerminalStyledSpan& span : row) {
    text += span.text;
  }
  return text;
}

std::optional<CompilerLocationMatch> terminal_row_link(const ConsolePanelState* state,
                                                         int row_index) {
  if (state == nullptr || row_index < 0 ||
      row_index >= static_cast<int>(state->terminal_styled_rows.size())) {
    return std::nullopt;
  }
  return find_compiler_location(terminal_row_text(state->terminal_styled_rows[static_cast<std::size_t>(row_index)]));
}

bool terminal_link_at_cell(const ConsolePanelState* state, int row_index, int col,
                           CompilerLocationMatch* out) {
  const auto link = terminal_row_link(state, row_index);
  if (!link.has_value() || out == nullptr) {
    return false;
  }
  if (col < link->span_start || col >= link->span_end) {
    return false;
  }
  *out = *link;
  return true;
}

bool update_terminal_link_hover(ConsolePanelState* state, MainLayoutState* layout_state, int x,
                                int y) {
  if (!hover_effects_enabled()) {
    return false;
  }
  if (state == nullptr || !terminal_box_valid(state->terminal_box) ||
      !state->terminal_box.Contain(x, y)) {
    const bool had_hover = state != nullptr && state->terminal_link_hover.has_value();
    if (state != nullptr) {
      state->terminal_link_hover.reset();
    }
    if (layout_state != nullptr) {
      layout_state->clickable.clear_hover_if(
          [](std::string_view id) { return id == press_id::kTerminalLink; });
    }
    return had_hover;
  }

  const int visual_row = y - state->terminal_box.y_min;
  const int row_index = state->terminal_first_visible + visual_row;
  const int col = x - state->terminal_box.x_min;

  CompilerLocationMatch match;
  std::optional<ConsolePanelState::TerminalLinkHover> hover;
  if (terminal_link_at_cell(state, row_index, col, &match)) {
    hover = ConsolePanelState::TerminalLinkHover{row_index, match.span_start, match.span_end};
  }

  const bool changed =
      hover.has_value() != state->terminal_link_hover.has_value() ||
      (hover.has_value() &&
       (hover->row != state->terminal_link_hover->row ||
        hover->span_start != state->terminal_link_hover->span_start ||
        hover->span_end != state->terminal_link_hover->span_end));
  state->terminal_link_hover = hover;
  if (layout_state != nullptr) {
    if (hover.has_value()) {
      layout_state->clickable.set_hover(press_id::kTerminalLink);
    } else {
      layout_state->clickable.clear_hover_if(
          [](std::string_view id) { return id == press_id::kTerminalLink; });
    }
  }
  return changed;
}

bool open_terminal_link(WorkspaceModel* workspace, DebugModel* model, FocusManagerState* focus,
                        MainLayoutState* layout_state, const CompilerLocationMatch& match) {
  if (workspace == nullptr || match.path.empty()) {
    return false;
  }
  const std::string cwd = model != nullptr ? model->workspace_root : workspace->root;
  const std::string resolved =
      resolve_compiler_path(match.path, workspace->root, cwd);
  workspace->record_cursor_jump();
  const int line = std::max(0, match.line - 1);
  const int col = match.column > 0 ? std::max(0, match.column - 1) : 0;
  if (!workspace->open_file_at(resolved, line, col)) {
    return false;
  }
  workspace->status_message =
      i18n::tr_fmt("status.navigate",
                   {std::filesystem::path(resolved).filename().string(), std::to_string(match.line),
                    std::to_string(match.column > 0 ? match.column : 1)});
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::None;
    layout_state->focus_sync_needed = true;
    layout_state->request_ui_tick = true;
  }
  return true;
}

void scroll_to_tail(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->first_visible = max_first_visible(total_lines, visible_lines);
  state->follow_tail = true;
}

void clamp_scroll(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->first_visible = std::max(
      0, std::min(state->first_visible, max_first_visible(total_lines, visible_lines)));
}

void clamp_terminal_scroll(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->terminal_first_visible = std::max(
      0, std::min(state->terminal_first_visible, max_first_visible(total_lines, visible_lines)));
}

void scroll_terminal_to_tail(ConsolePanelState* state, int total_lines, int visible_lines) {
  state->terminal_first_visible = max_first_visible(total_lines, visible_lines);
  state->terminal_follow_tail = true;
}

void follow_terminal_on_input(ConsolePanelState* state) {
  if (state == nullptr) {
    return;
  }
  state->terminal_follow_tail = true;
  const int total = static_cast<int>(state->terminal_styled_rows.size());
  const int visible = std::max(1, state->terminal_last_visible_lines);
  if (total > 0) {
    scroll_terminal_to_tail(state, total, visible);
  }
}

void request_terminal_repaint(MainLayoutState* layout_state) {
  if (layout_state != nullptr && layout_state->terminal_wake_callback) {
    layout_state->terminal_wake_callback();
    return;
  }
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
  nudge_terminal_repaint();
}

void scroll_terminal_by_lines(ConsolePanelState* state, int delta, int total_lines,
                              int visible_lines) {
  const int max_first = max_first_visible(total_lines, visible_lines);
  state->terminal_first_visible =
      std::max(0, std::min(state->terminal_first_visible + delta, max_first));
  state->terminal_follow_tail = state->terminal_first_visible >= max_first;
}

int terminal_viewport_lines(const ConsolePanelState* state, int viewport_height) {
  if (viewport_height > 0) {
    return viewport_height;
  }
  if (state == nullptr) {
    return 1;
  }
  int visible = visible_line_count(state->terminal_box);
  if (visible <= 1) {
    visible = visible_line_count(state->content_box);
  }
  if (visible <= 1 && state->panel_box.y_max > state->panel_box.y_min) {
    visible = std::max(1, visible_line_count(state->panel_box) - 2);
  }
  return std::max(1, visible);
}

bool handle_terminal_scroll_keys(ConsolePanelState* state, const Event& event) {
  if (state == nullptr || !state->shell_ui_active) {
    return false;
  }
  const int total = static_cast<int>(state->terminal_styled_rows.size());
  const int visible = state->terminal_last_visible_lines;
  if (event == Event::PageUp) {
    scroll_terminal_by_lines(state, -visible, total, visible);
    return true;
  }
  if (event == Event::PageDown) {
    scroll_terminal_by_lines(state, visible, total, visible);
    return true;
  }
  if (event == Event::Home) {
    state->terminal_first_visible = 0;
    state->terminal_follow_tail = false;
    return true;
  }
  if (event == Event::End) {
    scroll_terminal_to_tail(state, total, visible);
    return true;
  }
  return false;
}

bool apply_terminal_scrollbar_drag(ConsolePanelState* state, int mouse_y, int total_lines,
                                   int visible_lines) {
  if (state == nullptr || !state->terminal_scrollbar_layout.scrollable) {
    return false;
  }
  const int local_y = mouse_y - state->terminal_scrollbar_box.y_min;
  const int thumb_top = local_y - state->terminal_scrollbar_drag_offset;
  const int new_scroll = scroll_for_thumb_top(state->terminal_scrollbar_layout, thumb_top);
  if (state->terminal_first_visible != new_scroll) {
    state->terminal_first_visible = new_scroll;
    const int max_first = max_first_visible(total_lines, visible_lines);
    state->terminal_follow_tail = state->terminal_first_visible >= max_first;
  }
  return true;
}

bool handle_terminal_scroll_mouse(ConsolePanelState* state, MainLayoutState* layout_state,
                                  const Mouse& m, int total_lines, int visible_lines) {
  if (state == nullptr || total_lines <= 0) {
    return false;
  }

  const bool in_bar = state->terminal_scrollbar_box.Contain(m.x, m.y);
  const bool in_body = terminal_body_contains(state, m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->terminal_scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kTerminalScrollbar);
      } else {
        layout_state->clickable.clear_hover_if([&](std::string_view id) {
          return id == press_id::kTerminalScrollbar;
        });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (state->terminal_scrollbar_dragging) {
      return apply_terminal_scrollbar_drag(state, m.y, total_lines, visible_lines);
    }
    return in_bar;
  }

  if (state->terminal_scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->terminal_scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left) {
      return apply_terminal_scrollbar_drag(state, m.y, total_lines, visible_lines);
    }
  }

  if (in_body || in_bar) {
    if (m.button == Mouse::WheelUp) {
      scroll_terminal_by_lines(state, -3, total_lines, visible_lines);
      return true;
    }
    if (m.button == Mouse::WheelDown) {
      scroll_terminal_by_lines(state, 3, total_lines, visible_lines);
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button != Mouse::Left) {
    return false;
  }

  if (m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kTerminalScrollbar);
    const int local_y = m.y - state->terminal_scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->terminal_scrollbar_layout, state->terminal_scrollbar_box, m.x,
                            m.y)) {
      state->terminal_scrollbar_dragging = true;
      state->terminal_scrollbar_drag_offset = local_y - state->terminal_scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->terminal_scrollbar_layout.thumb_height / 2;
      state->terminal_first_visible =
          scroll_for_thumb_top(state->terminal_scrollbar_layout, thumb_top);
      state->terminal_scrollbar_dragging = true;
      state->terminal_scrollbar_drag_offset = state->terminal_scrollbar_layout.thumb_height / 2;
      const int max_first = max_first_visible(total_lines, visible_lines);
      state->terminal_follow_tail = state->terminal_first_visible >= max_first;
    }
    return true;
  }

  return false;
}

bool console_input_active(MainLayoutState* layout_state) {
  return layout_state &&
         layout_state->text_input_focus == TextInputFocus::Console;
}

bool shell_terminal_input_active(FocusManagerState* focus, ShellSession* shell) {
  return focus != nullptr && focus->region == FocusRegion::Terminal && shell != nullptr &&
         shell->running();
}

bool terminal_pty_input_active(MainLayoutState* layout_state, FocusManagerState* focus,
                               ShellSession* shell) {
  return shell_terminal_input_active(focus, shell) || console_input_active(layout_state);
}

bool debug_console_mode(AppMode* app_mode) {
  return app_mode != nullptr && *app_mode == AppMode::kDebug;
}

bool terminal_tab_active(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return true;
  }
  return layout_state->console_tabs.selected_tab == ConsolePanelTabs::kTerminal;
}

bool performance_tab_active(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return layout_state != nullptr &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kPerformance;
}

bool problems_tab_active_console(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return layout_state != nullptr &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kProblems;
}

bool search_tab_active_console(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return search_tab_active(layout_state);
}

bool call_hierarchy_tab_active_console(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return call_hierarchy_tab_active(layout_state);
}

bool core_analyzer_tab_visible(AppMode* app_mode, MainLayoutState* layout_state) {
  return debug_console_mode(app_mode) && layout_state != nullptr &&
         layout_state->show_core_analyzer_tab;
}

bool core_analyzer_tab_active(AppMode* app_mode, MainLayoutState* layout_state) {
  return core_analyzer_tab_visible(app_mode, layout_state) && layout_state != nullptr &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kCoreAnalyzer;
}

bool packet_monitor_tab_active_console(AppMode* app_mode, MainLayoutState* layout_state) {
  return debug_console_mode(app_mode) && packet_monitor_tab_active(layout_state);
}

bool git_tab_active_console(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return git_tab_active(layout_state);
}

bool binary_symbols_tab_active_console(AppMode* /*app_mode*/, MainLayoutState* layout_state) {
  return binary_symbols_tab_active(layout_state);
}

std::string_view console_tab_press_id(int tab) {
  switch (tab) {
    case ConsolePanelTabs::kTerminal:
      return press_id::kConsoleTabTerminal;
    case ConsolePanelTabs::kDebug:
      return press_id::kConsoleTabGdb;
    case ConsolePanelTabs::kPerformance:
      return press_id::kConsoleTabPerformance;
    case ConsolePanelTabs::kProblems:
      return press_id::kConsoleTabProblems;
    case ConsolePanelTabs::kSearch:
      return press_id::kConsoleTabSearch;
    case ConsolePanelTabs::kCallHierarchy:
      return press_id::kConsoleTabCallHierarchy;
    case ConsolePanelTabs::kGit:
      return press_id::kConsoleTabGit;
    case ConsolePanelTabs::kCoreAnalyzer:
      return press_id::kConsoleTabCoreAnalyzer;
    case ConsolePanelTabs::kBinarySymbols:
      return press_id::kConsoleTabBinarySymbols;
    case ConsolePanelTabs::kPacketMonitor:
      return press_id::kConsoleTabPacketMonitor;
    default:
      return press_id::kConsoleTabTerminal;
  }
}

int console_tab_from_digit(Event event, bool debug_mode) {
  if (event == Event::Character('1')) {
    return ConsolePanelTabs::kTerminal;
  }
  if (event == Event::Character('2')) {
    return debug_mode ? ConsolePanelTabs::kDebug : ConsolePanelTabs::kPerformance;
  }
  if (event == Event::Character('3')) {
    return debug_mode ? ConsolePanelTabs::kPerformance : ConsolePanelTabs::kProblems;
  }
  if (debug_mode && event == Event::Character('4')) {
    return ConsolePanelTabs::kProblems;
  }
  return -1;
}

bool debug_tab_active(AppMode* app_mode, MainLayoutState* layout_state) {
  return debug_console_mode(app_mode) && layout_state != nullptr &&
         layout_state->console_tabs.selected_tab == ConsolePanelTabs::kDebug;
}

Element make_tab_button(const std::string& label, bool selected, bool hovered, bool pressed,
                        Box* box) {
  return MakeTabButton(label, selected, hovered, pressed, box);
}

void activate_console_input(MainLayoutState* layout_state, FocusManagerState* focus,
                            Component input_box);

void activate_shell_input(DebugModel* model, MainLayoutState* layout_state,
                          FocusManagerState* focus, ShellSession* shell,
                          ConsolePanelState* state, const ShellLaunchConfig& launch_config);

bool handle_console_tab_hover(ConsolePanelState* state, MainLayoutState* layout_state,
                              AppMode* app_mode, const Mouse& mouse);

bool switch_console_tab(ConsolePanelState* state, MainLayoutState* layout_state,
                        FocusManagerState* focus, int tab, GitService* git,
                        GitPanelState* git_state) {
  if (layout_state == nullptr || tab < ConsolePanelTabs::kTerminal ||
      tab > ConsolePanelTabs::kPacketMonitor) {
    return false;
  }
  if (layout_state->console_tabs.selected_tab == tab) {
    return false;
  }
  const int previous_tab = layout_state->console_tabs.selected_tab;
  layout_state->console_tabs.selected_tab = tab;
  layout_state->focus_sync_needed = true;
  layout_state->request_ui_tick = true;
  if (tab == ConsolePanelTabs::kTerminal) {
    layout_state->text_input_focus = TextInputFocus::Console;
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
    if (state != nullptr) {
      state->terminal_resize_applied = false;
      state->terminal_view_valid = false;
    }
  } else {
    if (state != nullptr && previous_tab == ConsolePanelTabs::kTerminal) {
      state->content_box = Box{};
      state->terminal_box = Box{};
      state->terminal_view_valid = false;
    }
    if (layout_state->text_input_focus == TextInputFocus::Console &&
        tab != ConsolePanelTabs::kDebug) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    if (tab == ConsolePanelTabs::kDebug) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      layout_state->text_input_focus = TextInputFocus::Console;
    } else if (tab == ConsolePanelTabs::kSearch) {
      layout_state->right_sidebar.pending_focus_search = true;
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
    } else if (tab == ConsolePanelTabs::kCallHierarchy) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (is_search_input_focus(layout_state->text_input_focus)) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    } else if (tab == ConsolePanelTabs::kProblems) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (is_search_input_focus(layout_state->text_input_focus)) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    } else if (tab == ConsolePanelTabs::kCoreAnalyzer) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      layout_state->text_input_focus = TextInputFocus::Console;
      layout_state->core_analyzer_focus = MainLayoutState::CoreAnalyzerFocus::kCommand;
    } else if (tab == ConsolePanelTabs::kGit) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (is_search_input_focus(layout_state->text_input_focus)) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    } else if (tab == ConsolePanelTabs::kBinarySymbols) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (is_search_input_focus(layout_state->text_input_focus)) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    } else if (is_search_input_focus(layout_state->text_input_focus)) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    if (tab == ConsolePanelTabs::kGit && git != nullptr && git_state != nullptr) {
      GitPanelActivate(git, git_state);
    }
  }
  return true;
}

bool handle_console_tab_click(ConsolePanelState* state, MainLayoutState* layout_state,
                              FocusManagerState* focus, AppMode* app_mode, DebugModel* model,
                              ShellSession* shell, Component input_box,
                              const ShellLaunchConfig& launch_config, int mouse_x, int mouse_y,
                              GitService* git, GitPanelState* git_state) {
  if (state == nullptr) {
    return false;
  }
  const bool debug_mode = debug_console_mode(app_mode);
  for (int i = ConsolePanelTabs::kTerminal; i <= ConsolePanelTabs::kPacketMonitor; ++i) {
    if (i == ConsolePanelTabs::kDebug && !debug_mode) {
      continue;
    }
    if (i == ConsolePanelTabs::kPacketMonitor && !debug_mode) {
      continue;
    }
    if (i == ConsolePanelTabs::kCoreAnalyzer &&
        !core_analyzer_tab_visible(app_mode, layout_state)) {
      continue;
    }
    if (!state->tab_boxes[static_cast<std::size_t>(i)].Contain(mouse_x, mouse_y)) {
      continue;
    }
    trigger_press(layout_state, console_tab_press_id(i));
    switch_console_tab(state, layout_state, focus, i, git, git_state);
    if (i == ConsolePanelTabs::kTerminal) {
      activate_shell_input(model, layout_state, focus, shell, state, launch_config);
    } else if (i == ConsolePanelTabs::kDebug) {
      activate_console_input(layout_state, focus, input_box);
    } else if (i == ConsolePanelTabs::kCoreAnalyzer) {
      activate_console_input(layout_state, focus, input_box);
    } else if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  return false;
}

bool handle_console_panel_mouse(ConsolePanelState* state, MainLayoutState* layout_state,
                                FocusManagerState* focus, AppMode* app_mode, DebugModel* model,
                                WorkspaceModel* workspace, ShellSession* shell, Component input_box,
                                const ShellLaunchConfig& launch_config, Event event,
                                GitService* git, GitPanelState* git_state) {
  if (state == nullptr || layout_state == nullptr || !event.is_mouse()) {
    return false;
  }
  Mouse& m = event.mouse();
  const bool on_terminal_tab = terminal_tab_active(app_mode, layout_state);
  const bool on_debug_tab = debug_tab_active(app_mode, layout_state);

  if (m.motion == Mouse::Moved) {
    if (on_terminal_tab && state->shell_ui_active) {
      if (update_terminal_link_hover(state, layout_state, m.x, m.y)) {
        layout_state->request_ui_tick = true;
      }
    } else if (state->terminal_link_hover.has_value()) {
      state->terminal_link_hover.reset();
      if (layout_state != nullptr) {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kTerminalLink; });
        layout_state->request_ui_tick = true;
      }
    }
    if (handle_console_tab_hover(state, layout_state, app_mode, m)) {
      layout_state->request_ui_tick = true;
      return true;
    }
    if (on_terminal_tab && state->shell_ui_active) {
      const int term_total = static_cast<int>(state->terminal_styled_rows.size());
      const int term_visible = state->terminal_last_visible_lines;
      if (handle_terminal_scroll_mouse(state, layout_state, m, term_total, term_visible)) {
        return true;
      }
    }
    return false;
  }

  if (!state->panel_box.Contain(m.x, m.y)) {
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed &&
      state->hide_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, press_id::kConsoleHide);
    layout_state->console_visible = false;
    layout_state->request_ui_tick = true;
    return true;
  }

  if (on_terminal_tab && state->shell_ui_active) {
    const int term_total = static_cast<int>(state->terminal_styled_rows.size());
    const int term_visible = state->terminal_last_visible_lines;
    if (handle_terminal_scroll_mouse(state, layout_state, m, term_total, term_visible)) {
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Pressed &&
        terminal_body_contains(state, m.x, m.y)) {
      const int visual_row = m.y - state->terminal_box.y_min;
      const int row_index = state->terminal_first_visible + visual_row;
      const int col = m.x - state->terminal_box.x_min;
      CompilerLocationMatch match;
      if (terminal_link_at_cell(state, row_index, col, &match)) {
        trigger_press(layout_state, press_id::kTerminalLink);
        if (open_terminal_link(workspace, model, focus, layout_state, match)) {
          return true;
        }
      }
    }
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
  }

  if (handle_console_tab_click(state, layout_state, focus, app_mode, model, shell, input_box,
                               launch_config, m.x, m.y, git, git_state)) {
    layout_state->focus_sync_needed = true;
    return true;
  }
  if (on_debug_tab && state->history_box.Contain(m.x, m.y)) {
    layout_state->text_input_focus = TextInputFocus::None;
    layout_state->focus_sync_needed = true;
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
    return true;
  }
  if (on_terminal_tab && terminal_body_contains(state, m.x, m.y)) {
    activate_shell_input(model, layout_state, focus, shell, state, launch_config);
    layout_state->focus_sync_needed = true;
    return true;
  }
  if (on_debug_tab && state->input_box.Contain(m.x, m.y)) {
    activate_console_input(layout_state, focus, input_box);
    layout_state->focus_sync_needed = true;
    return true;
  }
  return false;
}

bool handle_console_tab_hover(ConsolePanelState* state, MainLayoutState* layout_state,
                              AppMode* app_mode, const Mouse& mouse) {
  if (state == nullptr || layout_state == nullptr || mouse.motion != Mouse::Moved) {
    return false;
  }
  const bool debug_mode = debug_console_mode(app_mode);
  if (debug_mode) {
    return update_panel_hover(
        layout_state, mouse.x, mouse.y,
        {{press_id::kConsoleTabTerminal, &state->tab_boxes[ConsolePanelTabs::kTerminal]},
         {press_id::kConsoleTabGdb, &state->tab_boxes[ConsolePanelTabs::kDebug]},
         {press_id::kConsoleTabPacketMonitor, &state->tab_boxes[ConsolePanelTabs::kPacketMonitor]},
         {press_id::kConsoleTabPerformance, &state->tab_boxes[ConsolePanelTabs::kPerformance]},
         {press_id::kConsoleTabProblems, &state->tab_boxes[ConsolePanelTabs::kProblems]},
         {press_id::kConsoleTabSearch, &state->tab_boxes[ConsolePanelTabs::kSearch]},
         {press_id::kConsoleTabCallHierarchy,
          &state->tab_boxes[ConsolePanelTabs::kCallHierarchy]},
         {press_id::kConsoleTabGit, &state->tab_boxes[ConsolePanelTabs::kGit]},
         {press_id::kConsoleTabBinarySymbols,
          &state->tab_boxes[ConsolePanelTabs::kBinarySymbols]},
         {press_id::kConsoleHide, &state->hide_box}},
        press_id::is_console_header_hover);
  }
  return update_panel_hover(
      layout_state, mouse.x, mouse.y,
      {{press_id::kConsoleTabTerminal, &state->tab_boxes[ConsolePanelTabs::kTerminal]},
       {press_id::kConsoleTabPerformance, &state->tab_boxes[ConsolePanelTabs::kPerformance]},
       {press_id::kConsoleTabProblems, &state->tab_boxes[ConsolePanelTabs::kProblems]},
       {press_id::kConsoleTabSearch, &state->tab_boxes[ConsolePanelTabs::kSearch]},
       {press_id::kConsoleTabCallHierarchy,
        &state->tab_boxes[ConsolePanelTabs::kCallHierarchy]},
       {press_id::kConsoleTabGit, &state->tab_boxes[ConsolePanelTabs::kGit]},
       {press_id::kConsoleTabBinarySymbols,
        &state->tab_boxes[ConsolePanelTabs::kBinarySymbols]},
       {press_id::kConsoleHide, &state->hide_box}},
      press_id::is_console_header_hover);
}

std::string console_placeholder(AppMode* /*app_mode*/) {
  return i18n::tr("console.placeholder.gdb");
}

constexpr int kMaxTermCols = 160;
constexpr int kMaxTermRows = 512;
constexpr int kTerminalScrollbarWidth = 1;

int clamp_terminal_cols(int cols) {
  return std::max(8, std::min(cols, kMaxTermCols));
}

int clamp_terminal_rows(int rows) {
  return std::max(2, std::min(rows, kMaxTermRows));
}

int measure_terminal_cols(const ConsolePanelState* state, int panel_width) {
  int cols = panel_width;
  if (state != nullptr && terminal_box_valid(state->panel_box)) {
    cols = visible_column_count(state->panel_box);
  }
  cols -= kTerminalScrollbarWidth;
  return clamp_terminal_cols(std::max(8, cols));
}

int measure_terminal_rows(int viewport_height) {
  return clamp_terminal_rows(std::max(2, viewport_height));
}

bool terminal_box_valid(const Box& box) {
  if (box.y_max <= box.y_min || box.x_max <= box.x_min) {
    return false;
  }
  const int rows = box.y_max - box.y_min + 1;
  const int cols = box.x_max - box.x_min + 1;
  return rows >= 1 && cols >= 4;
}

bool terminal_body_contains(const ConsolePanelState* state, int x, int y) {
  if (state == nullptr || !state->panel_box.Contain(x, y)) {
    return false;
  }
  if (terminal_box_valid(state->terminal_box) && state->terminal_box.Contain(x, y)) {
    return true;
  }
  const int body_top = state->panel_box.y_min + 2;
  return y >= body_top && y <= state->panel_box.y_max;
}

void update_terminal_layout(ConsolePanelState* state, int panel_height, int panel_width,
                            MainLayoutState* /*layout_state*/) {
  if (state == nullptr || panel_height <= 0) {
    return;
  }
  const int rows = measure_terminal_rows(panel_height);
  const int cols = measure_terminal_cols(state, panel_width);
  const bool size_changed =
      rows != state->pending_terminal_rows || cols != state->pending_terminal_cols;
  state->pending_terminal_rows = rows;
  state->pending_terminal_cols = cols;
  state->layout_measured = true;
  if (size_changed) {
    state->terminal_resize_applied = false;
    state->terminal_view_valid = false;
  }
}

Element make_terminal_panel(const std::string& title, Element body, int body_height) {
  return vbox({
             PanelTitle(title),
             std::move(body) | size(HEIGHT, EQUAL, std::max(1, body_height)),
         }) |
         bgcolor(theme::CodeBg());
}

Element render_terminal_body(Element content) {
  return std::move(content) | bgcolor(theme::CodeBg());
}

Element render_styled_line(const TerminalStyledRow& row, int cursor_col, bool show_cursor,
                           int max_cols, const std::optional<CompilerLocationMatch>& link,
                           bool link_hovered) {
  const Decorator cursor_cell = cursor_blink::cell_decorator();
  const bool draw_cursor = show_cursor;

  Elements parts;
  int col = 0;
  bool cursor_placed = false;
  for (const TerminalStyledSpan& span : row) {
    if (span.text.empty()) {
      continue;
    }
    for (char ch : span.text) {
      if (max_cols > 0 && col >= max_cols) {
        break;
      }
      Element cell = text(std::string(1, ch));
      const bool in_link =
          link.has_value() && col >= link->span_start && col < link->span_end;
      if (draw_cursor && cursor_col >= 0 && col == cursor_col) {
        cell = cell | cursor_cell;
        cursor_placed = true;
      } else if (in_link) {
        cell = cell | color(theme::Accent()) | underlined;
        if (link_hovered) {
          cell = cell | bold;
        }
      } else {
        cell = cell | color(span.fg) | bgcolor(span.bg);
      }
      parts.push_back(std::move(cell));
      ++col;
    }
    if (max_cols > 0 && col >= max_cols) {
      break;
    }
  }
  if (draw_cursor && cursor_col >= 0 && !cursor_placed && (max_cols <= 0 || cursor_col < max_cols)) {
    parts.push_back(text(" ") | cursor_cell);
    cursor_placed = true;
  }
  if (parts.empty()) {
    parts.push_back(text(" ") | (draw_cursor && cursor_col >= 0 ? cursor_cell
                                                                 : color(theme::WatchInput())));
  }
  Element line = hbox(std::move(parts));
  if (max_cols > 0) {
    line = line | size(WIDTH, EQUAL, max_cols);
  }
  return line | size(HEIGHT, EQUAL, 1);
}

Element render_terminal_styled(const std::vector<TerminalStyledRow>& rows, int first_visible,
                               int visible_count, int cursor_row, int cursor_col,
                               bool show_cursor, int max_cols,
                               const ConsolePanelState* state) {
  Elements lines;
  const int total = static_cast<int>(rows.size());
  const int end = std::min(total, first_visible + visible_count);
  for (int row = first_visible; row < end; ++row) {
    const int line_cursor = show_cursor && row == cursor_row ? cursor_col : -1;
    std::optional<CompilerLocationMatch> link = terminal_row_link(state, row);
    const bool link_hovered = state != nullptr && state->terminal_link_hover.has_value() &&
                              link.has_value() && state->terminal_link_hover->row == row &&
                              state->terminal_link_hover->span_start == link->span_start &&
                              state->terminal_link_hover->span_end == link->span_end;
    lines.push_back(render_styled_line(rows[static_cast<std::size_t>(row)], line_cursor,
                                       show_cursor && row == cursor_row, max_cols, link,
                                       link_hovered));
  }
  if (lines.empty()) {
    lines.push_back(text(" ") | size(HEIGHT, EQUAL, 1) | color(theme::WatchInput()));
  }
  return vbox(std::move(lines)) | bgcolor(theme::CodeBg());
}

void reset_terminal_session_state(ConsolePanelState* state, const std::string& workspace_root) {
  if (state == nullptr) {
    return;
  }
  if (state->last_workspace_root == workspace_root) {
    return;
  }
  state->last_workspace_root = workspace_root;
  state->shell_start_requested = false;
  state->shell_start_failed = false;
  state->shell_launch_uses_docker = false;
  state->shell_docker_container.clear();
  state->terminal_resize_applied = false;
  state->terminal_view_valid = false;
  state->terminal_styled_rows.clear();
  state->shell_ui_active = false;
  state->terminal_first_visible = 0;
  state->terminal_follow_tail = true;
  state->terminal_scrollbar_dragging = false;
  state->applied_terminal_cols = 0;
  state->applied_terminal_rows = 0;
}

void tick_terminal_shell(ConsolePanelState* state, ShellSession* shell,
                       const ShellLaunchConfig& launch_config) {
  if (state == nullptr || shell == nullptr) {
    return;
  }
  if (launch_config.host_cwd.empty()) {
    return;
  }

  reset_terminal_session_state(state, launch_config.host_cwd);

  const int cols =
      state->layout_measured ? state->pending_terminal_cols : state->pending_terminal_cols;
  const int rows =
      state->layout_measured ? state->pending_terminal_rows : state->pending_terminal_rows;

  if (!shell->running()) {
    state->shell_ui_active = false;
    if (shell->starting()) {
      return;
    }
    if (shell->start_failed()) {
      state->shell_start_failed = true;
      return;
    }
    if (!state->shell_start_requested || state->shell_start_failed) {
      return;
    }
    state->shell_launch_uses_docker = launch_config.uses_docker();
    state->shell_docker_container = launch_config.docker_container;
    shell->request_start(launch_config, cols, rows);
    return;
  }

  state->shell_start_failed = false;
  state->shell_ui_active = true;
  if (state->layout_measured &&
      (cols != state->applied_terminal_cols || rows != state->applied_terminal_rows ||
       !state->terminal_resize_applied)) {
    shell->resize(cols, rows);
    state->terminal_resize_applied = true;
    state->applied_terminal_cols = cols;
    state->applied_terminal_rows = rows;
    state->terminal_view_valid = false;
  }

  if (!state->layout_measured) {
    return;
  }
}

void refresh_terminal_view(ShellSession* shell, ConsolePanelState* state) {
  if (shell == nullptr || state == nullptr) {
    return;
  }
  TGDB_MON_SCOPE("shell", "refresh_terminal_view");
  state->shell_ui_active = shell->running();
  if (!state->shell_ui_active) {
    state->terminal_view_valid = false;
    return;
  }
  const bool pending = shell->consume_output_pending();
  const std::size_t queue_before = shell->pending_output_chunks();
  int drained = 0;
  while (shell->pending_output_chunks() > 0) {
    drained += shell->drain_output_bytes(8192);
  }
  if (drained > 0) {
    TGDB_MON("shell", "terminal drained_bytes=" + std::to_string(drained));
  }
  const std::string text = shell->display_text();
  if (text.empty() && drained == 0 && !pending && queue_before == 0) {
    return;
  }
  if (drained > 0 && text.empty()) {
    return;
  }
  if (text.empty()) {
    return;
  }
  if (drained == 0 && text == state->terminal_text && state->terminal_view_valid) {
    return;
  }
  state->terminal_text = text;
  state->terminal_styled_rows = shell->display_styled_rows();
  const int new_total = static_cast<int>(state->terminal_styled_rows.size());
  if (state->terminal_follow_tail) {
    scroll_terminal_to_tail(state, new_total, state->terminal_last_visible_lines);
  }
  clamp_terminal_scroll(state, new_total, state->terminal_last_visible_lines);
  state->terminal_view_valid = true;
}

bool forward_pty_key(ShellSession* shell, const Event& event, ConsolePanelState* state,
                     MainLayoutState* layout_state) {
  if (shell == nullptr || !shell->running()) {
    return false;
  }
  if (event.is_mouse() || event_is_tide_global_shortcut(event)) {
    return false;
  }
  const std::optional<std::string> bytes = event_to_pty_bytes(event);
  if (!bytes.has_value()) {
    return false;
  }
  shell->write_raw(*bytes);
  follow_terminal_on_input(state);
  request_terminal_repaint(layout_state);
  return true;
}

void activate_console_input(MainLayoutState* layout_state, FocusManagerState* focus,
                            Component input_box) {
  cursor_blink::show();
  if (layout_state) {
    layout_state->text_input_focus = TextInputFocus::Console;
    layout_state->focus_sync_needed = true;
  }
  if (focus) {
    focus->region = FocusRegion::Terminal;
  }
  if (input_box) {
    input_box->TakeFocus();
  }
}

void activate_shell_input(DebugModel* model, MainLayoutState* layout_state,
                          FocusManagerState* focus, ShellSession* shell,
                          ConsolePanelState* state, const ShellLaunchConfig& launch_config) {
  activate_console_input(layout_state, focus, nullptr);
  if (state != nullptr) {
    state->shell_start_requested = true;
    state->shell_start_failed = false;
  }
  tick_terminal_shell(state, shell, launch_config);
}

Element render_gdb_console(ConsolePanelState* state, DebugModel* model, AppMode* app_mode,
                           MainLayoutState* layout_state, Component input_box) {
  const std::vector<std::string>& terminal_lines = model->console_output;
  const int total = static_cast<int>(terminal_lines.size());

  int visible = visible_line_count(state->history_box);
  if (visible <= 1 && state->panel_box.y_max > state->panel_box.y_min) {
    visible = std::max(1, visible_line_count(state->panel_box) - 2);
  }
  state->last_visible_lines = visible;

  const std::size_t output_size = model->console_output.size();
  if (output_size > state->last_output_size) {
    if (state->follow_tail) {
      scroll_to_tail(state, total, visible);
    }
    state->last_output_size = output_size;
  } else if (output_size < state->last_output_size) {
    state->last_output_size = output_size;
    scroll_to_tail(state, total, visible);
  }

  clamp_scroll(state, total, visible);

  const int end = std::min(total, state->first_visible + visible);
  Elements history;
  for (int i = state->first_visible; i < end; ++i) {
    const std::string& line = terminal_lines[static_cast<std::size_t>(i)];
    history.push_back(text(line.empty() ? " " : line) | color(theme::Header()));
  }
  if (history.empty()) {
    history.push_back(text(i18n::tr("console.gdb.no_output")) | color(theme::Muted()));
  }

  const int rendered_lines = static_cast<int>(history.size());
  Element history_row =
      hbox({vbox(std::move(history)) | flex | reflect(state->history_box) |
                bgcolor(theme::PanelBg()),
            vertical_scrollbar(total, state->first_visible, visible, rendered_lines)}) |
      flex | bgcolor(theme::PanelBg());

  Element input_row;
  if (console_input_active(layout_state)) {
    input_row = input_box->Render() | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
                color(theme::WatchInput());
  } else {
    const std::string preview =
        state->input.empty() ? console_placeholder(app_mode) + i18n::tr("console.gdb.placeholder_click") : state->input;
    input_row = text(" " + preview + " ") | size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
                color(state->input.empty() ? theme::Muted() : theme::WatchInput());
  }
  input_row = input_row | reflect(state->input_box);

  return vbox({history_row | flex, separator() | size(HEIGHT, EQUAL, 1), input_row}) | flex;
}

Element render_shell_terminal(ConsolePanelState* state, DebugModel* model, ShellSession* shell,
                              FocusManagerState* focus, MainLayoutState* layout_state,
                              int viewport_height, int panel_width) {
  if (model->workspace_root.empty()) {
    return render_terminal_body(text(i18n::tr("console.terminal.select_workspace")) | color(theme::Muted()));
  }

  if (state == nullptr || !state->shell_ui_active) {
    std::string message = i18n::tr("console.terminal.open_hint");
    if (state != nullptr && state->shell_start_failed) {
      message = state->shell_launch_uses_docker
                    ? i18n::tr_fmt("console.terminal.docker_failed", {state->shell_docker_container})
                    : i18n::tr("console.terminal.shell_unavailable");
    } else if (state != nullptr && state->shell_start_requested && shell != nullptr &&
               shell->starting()) {
      message = state->shell_launch_uses_docker
                    ? i18n::tr_fmt("console.terminal.connecting_docker", {state->shell_docker_container})
                    : i18n::tr("console.terminal.starting");
    }
    return render_terminal_body(text(message) | color(theme::Muted()));
  }

  update_terminal_layout(state, viewport_height, panel_width, layout_state);

  if (!state->terminal_styled_rows.empty()) {
    const int total = static_cast<int>(state->terminal_styled_rows.size());
    const int visible = terminal_viewport_lines(state, viewport_height);
    const int display_cols = state->pending_terminal_cols;
    state->terminal_last_visible_lines = visible;
    if (state->terminal_follow_tail) {
      scroll_terminal_to_tail(state, total, visible);
    }
    clamp_terminal_scroll(state, total, visible);

    const bool at_tail = state->terminal_follow_tail;
    const bool show_cursor = shell_terminal_input_active(focus, shell) && at_tail;
    const int cursor_row = show_cursor && shell != nullptr ? shell->cursor_row() : -1;
    const int cursor_col = show_cursor && shell != nullptr ? shell->cursor_col() : -1;

    const int rendered_lines = std::min(visible, total - state->terminal_first_visible);
    Element content = render_terminal_styled(state->terminal_styled_rows, state->terminal_first_visible,
                                             visible, cursor_row, cursor_col, show_cursor,
                                             display_cols, state);

    const bool scroll_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kTerminalScrollbar);
    const bool scroll_active = state->terminal_scrollbar_dragging ||
                               (layout_state != nullptr &&
                                layout_state->clickable.is_pressed(press_id::kTerminalScrollbar));
    state->terminal_scrollbar_layout =
        compute_scrollbar_layout(total, state->terminal_first_visible, visible, rendered_lines);
    Element scrollbar =
        vertical_scrollbar(total, state->terminal_first_visible, visible, rendered_lines,
                         scroll_hovered, scroll_active) |
        reflect(state->terminal_scrollbar_box);

    return hbox({std::move(content) | flex | reflect(state->terminal_box), std::move(scrollbar)}) |
           bgcolor(theme::CodeBg());
  }
  return render_terminal_body(text("...") | color(theme::Muted()));
}

bool debug_console_keys_active(AppMode* app_mode, FocusManagerState* focus,
                               MainLayoutState* layout_state) {
  if (!debug_tab_active(app_mode, layout_state)) {
    return false;
  }
  if (console_input_active(layout_state)) {
    return true;
  }
  return focus != nullptr && focus->region == FocusRegion::Terminal;
}

bool handle_gdb_console_keys(AppMode* app_mode, DebugModel* model, ConsolePanelState* state,
                             MainLayoutState* layout_state, FocusManagerState* focus,
                             Component input_box, CommandCallback on_command, Event event) {
  if (!debug_tab_active(app_mode, layout_state) || model == nullptr || state == nullptr) {
    return false;
  }

  const int visible = state->last_visible_lines;
  const int total = static_cast<int>(model->console_output.size());
  const int max_first = max_first_visible(total, visible);

  if (event == Event::Custom && console_input_active(layout_state) && input_box) {
    input_box->TakeFocus();
    return false;
  }

  if (event == Event::Escape) {
    if (layout_state != nullptr && layout_state->editor_completion_open) {
      return false;
    }
    if (console_input_active(layout_state)) {
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }
  }

  if (event == Event::Return) {
    if (!debug_console_keys_active(app_mode, focus, layout_state)) {
      return false;
    }
    if (!console_input_active(layout_state)) {
      activate_console_input(layout_state, focus, input_box);
      return true;
    }
    const std::string line = state->input;
    state->input.clear();
    model->append_console("> " + line);
    handle_gdb_command(line, model, on_command);
    scroll_to_tail(state, total, visible);
    return true;
  }

  if (console_input_active(layout_state)) {
    if (!event.is_mouse() && event != Event::Custom && !event_is_tide_global_shortcut(event)) {
      cursor_blink::show();
      if (input_box) {
        input_box->TakeFocus();
        if (input_box->OnEvent(event)) {
          return true;
        }
      }
      if (event.is_character()) {
        const std::string ch = event.character();
        if (!ch.empty()) {
          state->input += ch;
          return true;
        }
      }
      if (event == Event::Backspace && !state->input.empty()) {
        state->input.pop_back();
        return true;
      }
      if (event == Event::Delete && !state->input.empty()) {
        state->input.pop_back();
        return true;
      }
      return false;
    }
    return false;
  }

  if (!debug_console_keys_active(app_mode, focus, layout_state)) {
    return false;
  }

  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->follow_tail = false;
    state->first_visible = std::max(0, state->first_visible - 1);
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->first_visible = std::min(state->first_visible + 1, max_first);
    if (state->first_visible >= max_first) {
      state->follow_tail = true;
    }
    return true;
  }
  if (event == Event::PageUp) {
    state->follow_tail = false;
    state->first_visible = std::max(0, state->first_visible - visible);
    return true;
  }
  if (event == Event::PageDown) {
    state->first_visible = std::min(state->first_visible + visible, max_first);
    if (state->first_visible >= max_first) {
      state->follow_tail = true;
    }
    return true;
  }
  if (event.is_mouse() && event.mouse().motion == Mouse::Pressed) {
    if (event.mouse().button == Mouse::WheelUp) {
      state->follow_tail = false;
      state->first_visible = std::max(0, state->first_visible - 3);
      return true;
    }
    if (event.mouse().button == Mouse::WheelDown) {
      state->first_visible = std::min(state->first_visible + 3, max_first);
      if (state->first_visible >= max_first) {
        state->follow_tail = true;
      }
      return true;
    }
  }

  return false;
}

ShellLaunchConfig current_shell_launch(const ShellLaunchConfigProvider& provider,
                                       DebugModel* model) {
  if (provider) {
    return provider();
  }
  ShellLaunchConfig fallback;
  if (model != nullptr) {
    fallback.host_cwd = model->workspace_root;
  }
  return fallback;
}

}  // namespace

bool cycle_console_tab(MainLayoutState* layout_state, FocusManagerState* focus, int delta,
                       AppMode* app_mode, GitService* git, GitPanelState* git_state) {
  if (layout_state == nullptr || delta == 0) {
    return false;
  }
  const bool debug_mode = app_mode != nullptr && *app_mode == AppMode::kDebug;
  int tab = layout_state->console_tabs.selected_tab;
  for (int attempt = 0; attempt < 10; ++attempt) {
    tab += delta;
    if (tab < ConsolePanelTabs::kTerminal) {
      tab = ConsolePanelTabs::kPacketMonitor;
    }
    if (tab > ConsolePanelTabs::kPacketMonitor) {
      tab = ConsolePanelTabs::kTerminal;
    }
    if (!debug_mode && tab == ConsolePanelTabs::kDebug) {
      continue;
    }
    if (!debug_mode && tab == ConsolePanelTabs::kPacketMonitor) {
      continue;
    }
    if (tab == ConsolePanelTabs::kCoreAnalyzer && layout_state != nullptr &&
        !layout_state->show_core_analyzer_tab) {
      continue;
    }
    break;
  }
  if (tab == layout_state->console_tabs.selected_tab) {
    return false;
  }
  return switch_console_tab(nullptr, layout_state, focus, tab, git, git_state);
}

Component MakeConsolePanel(AppMode* app_mode, DebugModel* model, ShellSession* shell,
                           CommandCallback on_command, MainLayoutState* layout_state,
                           FocusManagerState* focus, int* bottom_height,
                           ShellLaunchConfigProvider shell_launch_config,
                           WorkspaceModel* workspace,
                           std::shared_ptr<ISymbolProvider> symbols,
                           WorkspaceIndexer* indexer,
                           SymbolWorkspaceIndexer* symbol_indexer,
                           RightSidebarState* sidebar,
                           GitService* git_service,
                           GitPanelState* git_panel_state) {
  auto state = std::make_shared<ConsolePanelState>();
  state->input_placeholder = i18n::tr("console.placeholder.shell");
  auto perf_state = std::make_shared<PerformancePanelState>();
  auto packet_monitor_state = std::make_shared<PacketMonitorPanelState>();
  PerformanceSampler* sampler =
      layout_state != nullptr ? &layout_state->performance_sampler : nullptr;
  packet_monitor::PacketMonitorService* packet_monitor =
      layout_state != nullptr ? layout_state->packet_monitor_service.get() : nullptr;
  auto performance_panel = MakePerformancePanel(sampler, perf_state);
  auto packet_monitor_panel = MakePacketMonitorPanel(packet_monitor, packet_monitor_state, layout_state);
  auto diagnostics_panel =
      MakeDiagnosticsPanel(workspace, focus, symbols, layout_state, indexer);
  auto search_panel =
      MakeSearchPanel(workspace, model, focus, layout_state, indexer, sidebar);
  auto call_hierarchy_panel =
      MakeCallHierarchyPanel(workspace, focus, layout_state, sidebar, symbols);
  auto git_panel =
      MakeGitPanel(git_service, git_panel_state, layout_state, focus, workspace, &state->git_body_height);
  auto core_analyzer_panel =
      MakeCoreAnalyzerPanel(model, on_command, layout_state, focus);
  auto binary_symbols_panel =
      MakeBinarySymbolsPanel(workspace, model, focus, layout_state, symbols, symbol_indexer,
                             indexer, shell);
  auto input_box = Input(MakeBlinkInputOption(&state->input, &state->input_placeholder));
  auto input_maybe = Maybe(
      input_box, [app_mode, layout_state] {
        return debug_tab_active(app_mode, layout_state) && console_input_active(layout_state);
      });

  auto component = Container::Vertical(
      {input_maybe, performance_panel, diagnostics_panel, search_panel, call_hierarchy_panel,
       git_panel, core_analyzer_panel, binary_symbols_panel, packet_monitor_panel});

  auto wrapped = CatchEvent(component, [app_mode, model, shell, on_command, state,
                                        layout_state, focus, input_box,
                                        shell_launch_config, diagnostics_panel, search_panel,
                                        call_hierarchy_panel, git_panel, git_service,
                                        git_panel_state, core_analyzer_panel,
                                        binary_symbols_panel, packet_monitor_state,
                                        packet_monitor](Event event) {
    const bool editor_chrome_input =
        layout_state != nullptr &&
        is_editor_chrome_input_focus(layout_state->text_input_focus);
    if (problems_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        (focus == nullptr || focus->region == FocusRegion::Terminal) &&
        diagnostics_panel->OnEvent(event)) {
      return true;
    }
    if (search_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        (focus == nullptr || focus->region == FocusRegion::Terminal) &&
        search_panel->OnEvent(event)) {
      return true;
    }
    if (call_hierarchy_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        (focus == nullptr || focus->region == FocusRegion::Terminal)) {
      if (event == Event::Custom) {
        return true;
      }
      if (call_hierarchy_panel->OnEvent(event)) {
        return true;
      }
    }
    if (git_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        (focus == nullptr || focus->region == FocusRegion::Terminal)) {
      if (git_panel->OnEvent(event)) {
        return true;
      }
    }
    if (core_analyzer_tab_active(app_mode, layout_state) && !editor_chrome_input &&
        (focus == nullptr || focus->region == FocusRegion::Terminal)) {
      if (core_analyzer_panel->OnEvent(event)) {
        return true;
      }
    }
    if (binary_symbols_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        event != Event::Custom) {
      if (binary_symbols_panel->OnEvent(event)) {
        return true;
      }
    }
    if (packet_monitor_tab_active_console(app_mode, layout_state) && !editor_chrome_input &&
        layout_state != nullptr) {
      if (handle_packet_monitor_keys(event, packet_monitor, packet_monitor_state.get(),
                                     layout_state)) {
        return true;
      }
    }

    const bool on_terminal_tab = terminal_tab_active(app_mode, layout_state);
    const bool on_debug_tab = debug_tab_active(app_mode, layout_state);

    if (on_terminal_tab && event == Event::Custom) {
      if (focus != nullptr && focus->region == FocusRegion::Terminal && layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::Console;
      }
      return false;
    }

    if (on_debug_tab && event == Event::Custom && console_input_active(layout_state)) {
      input_box->TakeFocus();
      return false;
    }

    if (const int tab = console_tab_from_digit(event, debug_console_mode(app_mode)); tab >= 0) {
      trigger_press(layout_state, console_tab_press_id(tab));
      switch_console_tab(state.get(), layout_state, focus, tab, git_service, git_panel_state);
      if (tab == ConsolePanelTabs::kTerminal) {
        activate_shell_input(model, layout_state, focus, shell, state.get(),
                             current_shell_launch(shell_launch_config, model));
      } else if (tab == ConsolePanelTabs::kDebug) {
        activate_console_input(layout_state, focus, input_box);
      }
      return true;
    }

    if (terminal_pty_input_active(layout_state, focus, shell) && on_terminal_tab &&
        event_is_ctrl_c(event)) {
      if (shell != nullptr && shell->running()) {
        shell->send_interrupt();
      }
      return true;
    }

    if (event == Event::Escape) {
      if (layout_state != nullptr && layout_state->editor_completion_open) {
        return false;
      }
      if (!on_debug_tab && layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
        return true;
      }
    }

    if (on_terminal_tab && state->shell_ui_active && handle_terminal_scroll_keys(state.get(), event)) {
      return true;
    }

    if (terminal_pty_input_active(layout_state, focus, shell) && on_terminal_tab) {
      if (!event_is_tide_global_shortcut(event) &&
          forward_pty_key(shell, event, state.get(), layout_state)) {
        return true;
      }
      if (event == Event::Escape) {
        return true;
      }
      return false;
    }

    if (event == Event::Return && on_terminal_tab) {
      activate_shell_input(model, layout_state, focus, shell, state.get(),
                           current_shell_launch(shell_launch_config, model));
      if (forward_pty_key(shell, event, state.get(), layout_state)) {
        return true;
      }
      return true;
    }

    if (handle_gdb_console_keys(app_mode, model, state.get(), layout_state, focus, input_box,
                                on_command, event)) {
      return true;
    }

    return false;
  });

  auto dispatch_console_keys = [app_mode, layout_state, focus, shell, state, model, on_command,
                                input_box](Event event) -> bool {
    if (handle_gdb_console_keys(app_mode, model, state.get(), layout_state, focus, input_box,
                                on_command, event)) {
      return true;
    }
    if (!terminal_tab_active(app_mode, layout_state)) {
      return false;
    }
    if (state->shell_ui_active && terminal_pty_input_active(layout_state, focus, shell) &&
        handle_terminal_scroll_keys(state.get(), event)) {
      return true;
    }
    if (!terminal_pty_input_active(layout_state, focus, shell)) {
      return false;
    }
    if (event_is_tide_global_shortcut(event)) {
      return false;
    }
    if (event_is_ctrl_c(event)) {
      if (shell != nullptr && shell->running()) {
        shell->send_interrupt();
      }
      return true;
    }
    if (event == Event::Escape) {
      if (layout_state != nullptr && layout_state->editor_completion_open) {
        return false;
      }
      if (layout_state) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
      return true;
    }
    return forward_pty_key(shell, event, state.get(), layout_state);
  };

  auto dispatch_packet_monitor_keys =
      [packet_monitor, packet_monitor_state, layout_state](const Event& event) -> bool {
    if (!packet_monitor_tab_active(layout_state)) {
      return false;
    }
    return handle_packet_monitor_keys(event, packet_monitor, packet_monitor_state.get(),
                                      layout_state);
  };

  auto dispatch_packet_monitor_mouse =
      [packet_monitor, packet_monitor_state, layout_state, focus](const Event& event) -> bool {
    if (!packet_monitor_tab_active(layout_state)) {
      return false;
    }
    return handle_packet_monitor_mouse(event, packet_monitor, packet_monitor_state.get(),
                                       layout_state, focus);
  };

  auto dispatch_console_mouse = [app_mode, layout_state, focus, model, workspace, shell, state, input_box,
                                 shell_launch_config, diagnostics_panel, search_panel,
                                 call_hierarchy_panel, git_panel, git_service,
                                 git_panel_state, core_analyzer_panel, binary_symbols_panel,
                                 dispatch_packet_monitor_mouse](Event event) -> bool {
    if (event.is_mouse()) {
      if (problems_tab_active_console(app_mode, layout_state) &&
          layout_state != nullptr && layout_state->problems_key_handler &&
          layout_state->problems_key_handler(event)) {
        return true;
      }
      if (problems_tab_active_console(app_mode, layout_state) &&
          diagnostics_panel->OnEvent(event)) {
        return true;
      }
      if (search_tab_active_console(app_mode, layout_state) && search_panel->OnEvent(event)) {
        return true;
      }
      if (call_hierarchy_tab_active_console(app_mode, layout_state) &&
          call_hierarchy_panel->OnEvent(event)) {
        return true;
      }
      if (git_tab_active_console(app_mode, layout_state) && git_panel->OnEvent(event)) {
        return true;
      }
      if (core_analyzer_tab_active(app_mode, layout_state) &&
          core_analyzer_panel->OnEvent(event)) {
        return true;
      }
      if (binary_symbols_tab_active_console(app_mode, layout_state) &&
          binary_symbols_panel->OnEvent(event)) {
        return true;
      }
      if (packet_monitor_tab_active_console(app_mode, layout_state) &&
          dispatch_packet_monitor_mouse(event)) {
        return true;
      }
    }
    return handle_console_panel_mouse(
        state.get(), layout_state, focus, app_mode, model, workspace, shell, input_box,
        current_shell_launch(shell_launch_config, model), event, git_service, git_panel_state);
  };

  if (layout_state != nullptr) {
    layout_state->console_key_handler = dispatch_console_keys;
    layout_state->console_mouse_handler = dispatch_console_mouse;
    layout_state->console_debug_mouse_handler = dispatch_console_mouse;
    layout_state->packet_monitor_key_handler = dispatch_packet_monitor_keys;
    layout_state->packet_monitor_mouse_handler = dispatch_packet_monitor_mouse;
    layout_state->terminal_follow_input_callback = [state, layout_state]() {
      follow_terminal_on_input(state.get());
      request_terminal_repaint(layout_state);
    };
    if (shell != nullptr) {
      shell->set_output_notify([layout_state]() { request_terminal_repaint(layout_state); });
    }
    layout_state->terminal_tick_callback = [app_mode, model, shell, state, layout_state, focus,
                                            bottom_height, shell_launch_config] {
      if (layout_state->terminal_start_requested) {
        state->shell_start_requested = true;
        state->shell_start_failed = false;
      }
      if (focus != nullptr && focus->region == FocusRegion::Terminal &&
          terminal_tab_active(app_mode, layout_state)) {
        layout_state->text_input_focus = TextInputFocus::Console;
      }
      const int panel_height =
          bottom_height != nullptr && *bottom_height > 1 ? *bottom_height : 8;
      const int layout_height = std::max(2, panel_height - 2);
      const int panel_width = state->panel_box.x_max >= state->panel_box.x_min
                                  ? state->panel_box.x_max - state->panel_box.x_min + 1
                                  : 80;
      if (!terminal_tab_active(app_mode, layout_state)) {
        return;
      }
      update_terminal_layout(state.get(), layout_height, panel_width, layout_state);
      tick_terminal_shell(state.get(), shell,
                          current_shell_launch(shell_launch_config, model));
      refresh_terminal_view(shell, state.get());
    };
  }

  return Renderer(wrapped, [app_mode, model, shell, focus, input_box, input_maybe, state,
                            layout_state, bottom_height, perf_state, sampler, diagnostics_panel,
                            search_panel, call_hierarchy_panel, git_panel, core_analyzer_panel,
                            binary_symbols_panel, packet_monitor_panel] {
    state->input_placeholder = console_placeholder(app_mode);
    (void)input_maybe;
    (void)input_box;

    const bool debug_mode = debug_console_mode(app_mode);
    const int panel_height =
        bottom_height != nullptr && *bottom_height > 1 ? *bottom_height : 8;
    const int selected_tab = layout_state != nullptr ? layout_state->console_tabs.selected_tab
                                                     : ConsolePanelTabs::kTerminal;
    const int body_height = std::max(1, panel_height - 2);
    state->git_body_height = std::max(1, body_height - 2);
    const int panel_width = state->panel_box.x_max >= state->panel_box.x_min
                                ? state->panel_box.x_max - state->panel_box.x_min + 1
                                : 80;

    Elements tab_row;
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.terminal"), selected_tab == ConsolePanelTabs::kTerminal,
        layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kConsoleTabTerminal),
        layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kConsoleTabTerminal),
        &state->tab_boxes[ConsolePanelTabs::kTerminal]));
    if (debug_mode) {
      tab_row.push_back(make_tab_button(
          i18n::tr("console.tab.gdb"), selected_tab == ConsolePanelTabs::kDebug,
          layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kConsoleTabGdb),
          layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kConsoleTabGdb),
          &state->tab_boxes[ConsolePanelTabs::kDebug]));
      tab_row.push_back(make_tab_button(
          i18n::tr("console.tab.packet_monitor"),
          selected_tab == ConsolePanelTabs::kPacketMonitor,
          layout_state != nullptr &&
              layout_state->clickable.is_hovered(press_id::kConsoleTabPacketMonitor),
          layout_state != nullptr &&
              layout_state->clickable.is_pressed(press_id::kConsoleTabPacketMonitor),
          &state->tab_boxes[ConsolePanelTabs::kPacketMonitor]));
      if (layout_state != nullptr && layout_state->show_core_analyzer_tab) {
        tab_row.push_back(make_tab_button(
            i18n::tr("console.tab.core_analyzer"), selected_tab == ConsolePanelTabs::kCoreAnalyzer,
            layout_state->clickable.is_hovered(press_id::kConsoleTabCoreAnalyzer),
            layout_state->clickable.is_pressed(press_id::kConsoleTabCoreAnalyzer),
            &state->tab_boxes[ConsolePanelTabs::kCoreAnalyzer]));
      }
    }
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.performance"), selected_tab == ConsolePanelTabs::kPerformance,
        layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kConsoleTabPerformance),
        layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kConsoleTabPerformance),
        &state->tab_boxes[ConsolePanelTabs::kPerformance]));
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.problems"), selected_tab == ConsolePanelTabs::kProblems,
        layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kConsoleTabProblems),
        layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kConsoleTabProblems),
        &state->tab_boxes[ConsolePanelTabs::kProblems]));
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.search"), selected_tab == ConsolePanelTabs::kSearch,
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kConsoleTabSearch),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kConsoleTabSearch),
        &state->tab_boxes[ConsolePanelTabs::kSearch]));
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.call_hierarchy"), selected_tab == ConsolePanelTabs::kCallHierarchy,
        layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kConsoleTabCallHierarchy),
        layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kConsoleTabCallHierarchy),
        &state->tab_boxes[ConsolePanelTabs::kCallHierarchy]));
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.git"), selected_tab == ConsolePanelTabs::kGit,
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kConsoleTabGit),
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kConsoleTabGit),
        &state->tab_boxes[ConsolePanelTabs::kGit]));
    tab_row.push_back(make_tab_button(
        i18n::tr("console.tab.symbols"), selected_tab == ConsolePanelTabs::kBinarySymbols,
        layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kConsoleTabBinarySymbols),
        layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kConsoleTabBinarySymbols),
        &state->tab_boxes[ConsolePanelTabs::kBinarySymbols]));

    const bool hide_hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kConsoleHide);
    const bool hide_pressed =
        layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kConsoleHide);
    Element hide_btn = MakeToolbarButton(text(i18n::tr("console.hide_panel")) | color(theme::Muted()), hide_hovered,
                                         hide_pressed, false, &state->hide_box);

    Element body;
    if (selected_tab == ConsolePanelTabs::kTerminal) {
      body = render_shell_terminal(state.get(), model, shell, focus, layout_state, body_height,
                                   panel_width) |
             flex;
    } else if (selected_tab == ConsolePanelTabs::kDebug) {
      body = render_gdb_console(state.get(), model, app_mode, layout_state, input_box);
    } else if (selected_tab == ConsolePanelTabs::kCoreAnalyzer) {
      body = core_analyzer_panel->Render() | flex;
    } else if (selected_tab == ConsolePanelTabs::kPerformance) {
      body = RenderPerformancePanel(sampler, perf_state.get(), panel_width, body_height) | flex;
    } else if (selected_tab == ConsolePanelTabs::kProblems) {
      body = diagnostics_panel->Render() | flex;
    } else if (selected_tab == ConsolePanelTabs::kSearch) {
      body = search_panel->Render() | flex;
    } else if (selected_tab == ConsolePanelTabs::kGit) {
      body = git_panel->Render() | flex;
    } else if (selected_tab == ConsolePanelTabs::kBinarySymbols) {
      body = binary_symbols_panel->Render() | flex;
    } else if (selected_tab == ConsolePanelTabs::kPacketMonitor) {
      body = packet_monitor_panel->Render() | flex;
    } else {
      body = call_hierarchy_panel->Render() | flex;
    }

    return vbox({
               hbox({hbox(std::move(tab_row)), filler(),
                     hide_btn | size(WIDTH, EQUAL, 3)}) |
                   size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()),
               separator() | size(HEIGHT, EQUAL, 1),
               std::move(body) | flex,
           }) |
           reflect(state->panel_box) | flex | bgcolor(theme::PanelBg());
  });
}

}  // namespace tgdb
