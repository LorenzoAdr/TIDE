#include "ui/diagnostics_panel.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "lsp/diagnostics.hpp"
#include "editor/editor_state.hpp"
#include "symbols/code_action.hpp"
#include "ui/clickable.hpp"
#include "ui/focus_manager.hpp"
#include "ui/focusable_component.hpp"
#include "ui/context_menu.hpp"
#include "ui/hover_effects.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tuide {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

enum class FixAvailability { kUnknown, kNone, kAvailable };

struct DiagnosticRow {
  std::string path;
  int line = 0;
  int character = 0;
  int end_col = 0;
  std::string message;
  std::string source;
  DiagnosticSeverity severity = DiagnosticSeverity::kError;
  FixAvailability fix = FixAvailability::kUnknown;
};

struct DiagnosticsPanelState {
  std::vector<DiagnosticRow> rows;
  std::vector<Box> fix_boxes;
  int selected = 0;
  int first_visible = 0;
  int last_visible_lines = 1;
  Box content_box;
  Box list_content_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  uint64_t rows_revision = 0;
};

Color severity_color(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kError:
      return theme::Error();
    case DiagnosticSeverity::kWarning:
      return theme::Warning();
    case DiagnosticSeverity::kInfo:
      return theme::Accent();
    case DiagnosticSeverity::kHint:
    default:
      return theme::Muted();
  }
}

char severity_marker(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kError:
      return '!';
    case DiagnosticSeverity::kWarning:
      return 'W';
    default:
      return ' ';
  }
}

std::string display_path(const std::string& absolute, const std::string& workspace_root) {
  if (absolute.empty()) {
    return {};
  }
  if (!workspace_root.empty()) {
    std::error_code ec;
    const auto rel = fs::relative(fs::path(absolute), fs::path(workspace_root), ec);
    if (!ec) {
      return rel.generic_string();
    }
  }
  return fs::path(absolute).filename().string();
}

// See editor_panel.cpp's buffer_text(): unified onto the cached,
// backend-agnostic-O(n) editor_buffer_joined_source().
std::string buffer_text(const EditorBuffer& buffer) { return editor_buffer_joined_source(buffer); }

std::vector<std::string> workspace_relative_files(WorkspaceIndexer* indexer) {
  if (indexer == nullptr) {
    return {};
  }
  const auto snapshot = indexer->snapshot();
  if (!snapshot) {
    return {};
  }
  return snapshot->files;
}

std::vector<DiagnosticRow> build_rows(WorkspaceModel* workspace,
                                      const std::shared_ptr<ISymbolProvider>& symbols,
                                      WorkspaceIndexer* indexer, MainLayoutState* layout_state) {
  std::vector<DiagnosticRow> rows;
  if (!symbols || !symbols->supports_diagnostics()) {
    return rows;
  }

  const std::string active =
      workspace != nullptr && !workspace->buffer.path.empty() ? workspace->buffer.path
                                                              : std::string{};
  if (active.empty()) {
    return rows;
  }

  const int64_t last_edit_ms = workspace != nullptr ? workspace->last_buffer_edit_ms : 0;
  if (!diagnostics_display_allowed(last_edit_ms, symbols.get(), active,
                                   layout_state != nullptr &&
                                       layout_state->activity_gate.allows_lsp_ui())) {
    return rows;
  }

  const std::string workspace_root = workspace != nullptr ? workspace->root : std::string{};
  const std::string active_text =
      workspace != nullptr ? buffer_text(workspace->buffer) : std::string{};
  const auto docs = diagnostics_for_translation_unit(
      symbols->workspace_diagnostics(), active, workspace_root,
      workspace_relative_files(indexer), active_text);

  for (const auto& doc : docs) {
    for (const auto& item : doc.items) {
      DiagnosticRow row;
      row.path = doc.path;
      row.line = item.line;
      row.character = item.start_col;
      row.end_col = item.end_col;
      row.message = item.message;
      row.source = item.source;
      row.severity = item.severity;
      rows.push_back(std::move(row));
    }
  }

  std::sort(rows.begin(), rows.end(), [&active](const DiagnosticRow& a, const DiagnosticRow& b) {
    const bool a_active = !active.empty() && a.path == active;
    const bool b_active = !active.empty() && b.path == active;
    if (a_active != b_active) {
      return a_active > b_active;
    }
    if (a.path != b.path) {
      return a.path < b.path;
    }
    if (a.line != b.line) {
      return a.line < b.line;
    }
    return a.character < b.character;
  });
  return rows;
}

void navigate_to_diagnostic(WorkspaceModel* workspace, const DiagnosticRow& row) {
  if (workspace == nullptr || row.path.empty()) {
    return;
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(row.path, row.line, row.character);
  workspace->status_message = i18n::tr_fmt("status.navigate", {fs::path(row.path).filename().string(), std::to_string(row.line + 1), std::to_string(row.character + 1)});
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

bool content_box_laid_out(const Box& box) {
  return box.y_max > box.y_min || box.x_max > box.x_min;
}

constexpr int kLinesPerProblem = 2;

int panel_content_width(const Box& box) {
  if (box.x_max < box.x_min) {
    return 80;
  }
  return std::max(1, box.x_max - box.x_min + 1);
}

int visible_problem_count(int visible_terminal_lines) {
  return std::max(1, (visible_terminal_lines + kLinesPerProblem - 1) / kLinesPerProblem);
}

int problems_visible_terminal_lines(const Box& content_box, MainLayoutState* layout_state,
                                    int total_problems, int last_visible_lines) {
  int visible = visible_line_count(content_box);
  // First paint (and any rebuild before reflect): content_box is still empty, so
  // visible_line_count returns 1. Problems sits behind UiPanelRenderCache; if we
  // only emit one row, that Element is frozen until another panel dirties Console.
  if (!content_box_laid_out(content_box) || (visible <= 1 && total_problems > 1)) {
    visible = std::max({1, last_visible_lines, total_problems * kLinesPerProblem});
    if (layout_state != nullptr && layout_state->terminal_height) {
      visible = std::max(visible, std::max(6, layout_state->terminal_height() / 3));
    }
  }
  return visible;
}

std::string format_problem_line(const DiagnosticRow& row, const std::string& workspace_root) {
  const std::string loc =
      display_path(row.path, workspace_root) + ":" + std::to_string(row.line + 1) + " ";
  const char marker = severity_marker(row.severity);
  return " " + std::string(1, marker) + " " + loc + row.message;
}

std::array<std::string, kLinesPerProblem> wrap_problem_line(const std::string& text,
                                                            int max_width) {
  std::array<std::string, kLinesPerProblem> lines = {"", ""};
  if (text.empty() || max_width <= 0) {
    lines[0] = text;
    return lines;
  }
  if (static_cast<int>(text.size()) <= max_width) {
    lines[0] = text;
    return lines;
  }

  size_t break_at = static_cast<size_t>(max_width);
  const size_t space = text.rfind(' ', break_at);
  if (space != std::string::npos && space > 0) {
    break_at = space;
  }
  lines[0] = text.substr(0, break_at);

  std::string rest = text.substr(break_at);
  while (!rest.empty() && rest.front() == ' ') {
    rest.erase(rest.begin());
  }
  if (rest.empty()) {
    return lines;
  }

  if (static_cast<int>(rest.size()) <= max_width) {
    lines[1] = rest;
    return lines;
  }

  if (max_width <= 3) {
    lines[1] = "...";
    return lines;
  }

  size_t second_break = static_cast<size_t>(max_width - 3);
  const size_t second_space = rest.rfind(' ', second_break);
  if (second_space != std::string::npos && second_space > 0) {
    second_break = second_space;
  }
  lines[1] = rest.substr(0, second_break) + "...";
  return lines;
}

bool code_actions_available(const std::shared_ptr<ISymbolProvider>& symbols,
                            MainLayoutState* layout_state) {
  return symbols != nullptr && symbols->supports_code_actions() &&
         (layout_state == nullptr || layout_state->app_settings == nullptr ||
          layout_state->app_settings->lsp_enabled);
}

std::string document_text_for_path(WorkspaceModel* workspace, const std::string& path) {
  if (workspace == nullptr || path.empty()) {
    return {};
  }
  const int tab = workspace->find_tab(path);
  if (tab >= 0) {
    return buffer_text(workspace->tabs[static_cast<std::size_t>(tab)].buffer);
  }
  if (!workspace->buffer.path.empty() && workspace->buffer.path == path) {
    return buffer_text(workspace->buffer);
  }
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream ss;
  ss << input.rdbuf();
  return ss.str();
}

bool diagnostic_has_quick_fix(WorkspaceModel* workspace,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              const DiagnosticRow& row) {
  if (symbols == nullptr || row.path.empty()) {
    return false;
  }
  CodeActionParams params;
  params.path = row.path;
  params.text = document_text_for_path(workspace, row.path);
  params.line = row.line;
  params.start_col = row.character;
  params.end_col = row.end_col;
  params.diagnostic.line = row.line;
  params.diagnostic.start_col = row.character;
  params.diagnostic.end_col = row.end_col;
  params.diagnostic.message = row.message;
  params.diagnostic.source = row.source.empty() ? "clang" : row.source;
  params.diagnostic.severity = row.severity;

  const std::vector<CodeActionItem> actions = symbols->code_actions_for_diagnostic(params);
  for (const CodeActionItem& action : actions) {
    if (!action.file_edits.empty()) {
      return true;
    }
  }
  return false;
}

// Probe at most one unknown visible row per call so the UI stays responsive.
// Returns true if more unknowns remain in the visible window (caller should wake).
bool probe_visible_fix_availability(DiagnosticsPanelState* state, WorkspaceModel* workspace,
                                      const std::shared_ptr<ISymbolProvider>& symbols,
                                      MainLayoutState* layout_state, int first_visible,
                                      int end_visible) {
  if (state == nullptr) {
    return false;
  }
  const bool available = code_actions_available(symbols, layout_state);
  bool probed = false;
  bool more_unknown = false;
  for (int i = first_visible; i < end_visible; ++i) {
    DiagnosticRow& row = state->rows[static_cast<std::size_t>(i)];
    if (row.fix != FixAvailability::kUnknown) {
      continue;
    }
    if (probed) {
      more_unknown = true;
      break;
    }
    if (!available) {
      row.fix = FixAvailability::kNone;
    } else {
      row.fix = diagnostic_has_quick_fix(workspace, symbols, row) ? FixAvailability::kAvailable
                                                                   : FixAvailability::kNone;
    }
    probed = true;
  }
  if (!probed) {
    return false;
  }
  for (int i = first_visible; i < end_visible; ++i) {
    if (state->rows[static_cast<std::size_t>(i)].fix == FixAvailability::kUnknown) {
      more_unknown = true;
      break;
    }
  }
  return more_unknown;
}

int fix_button_width() {
  const std::string label = i18n::tr("panel.problems.fix");
  // MakeToolbarButton(compact) wraps content; content includes leading/trailing spaces.
  return static_cast<int>(label.size()) + 2;
}

Element make_problem_row_element(const DiagnosticRow& row, const std::string& workspace_root,
                                 int panel_width, bool selected, bool show_fix, bool fix_hovered,
                                 bool fix_pressed, Box* fix_box) {
  const int text_width =
      show_fix ? std::max(1, panel_width - fix_button_width() - 1) : panel_width;
  const auto wrapped = wrap_problem_line(format_problem_line(row, workspace_root), text_width);
  Element first = text(wrapped[0]) | size(HEIGHT, EQUAL, 1);
  Element second = text(wrapped[1]) | size(HEIGHT, EQUAL, 1);
  Element row_el = vbox({std::move(first), std::move(second)});
  if (selected) {
    row_el = row_el | inverted | bold;
  } else {
    row_el = row_el | color(severity_color(row.severity));
  }
  if (!show_fix || fix_box == nullptr) {
    return row_el;
  }
  Element fix_btn = MakeToolbarButton(text(" " + i18n::tr("panel.problems.fix") + " "),
                                      fix_hovered, fix_pressed, false, fix_box, true);
  return hbox({
      std::move(row_el) | flex,
      vbox({filler(), std::move(fix_btn), filler()}),
  });
}

bool apply_fix_for_row(WorkspaceModel* workspace, DebugModel* model,
                        MainLayoutState* layout_state,
                        const std::shared_ptr<ISymbolProvider>& symbols,
                        SymbolWorkspaceIndexer* symbol_indexer, const DiagnosticRow& row) {
  return apply_diagnostic_quick_fix(workspace, model, layout_state, symbols, symbol_indexer,
                                    row.path, row.line, row.character, row.end_col, row.message,
                                    row.severity, row.source);
}

void clamp_scroll_viewport(DiagnosticsPanelState* state, int visible_terminal_lines) {
  const int total = static_cast<int>(state->rows.size());
  const int visible_rows = visible_problem_count(visible_terminal_lines);
  const int max_first = std::max(0, total - visible_rows);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
}

bool scroll_problems_by_delta(DiagnosticsPanelState* state, MainLayoutState* layout_state,
                              int delta) {
  if (state == nullptr || delta == 0) {
    return false;
  }
  const int visible = problems_visible_terminal_lines(
      state->content_box, layout_state, static_cast<int>(state->rows.size()),
      state->last_visible_lines);
  state->last_visible_lines = visible;
  const int visible_rows = visible_problem_count(visible);
  const int total = static_cast<int>(state->rows.size());
  const int max_first = std::max(0, total - visible_rows);
  state->first_visible = std::max(0, std::min(state->first_visible + delta, max_first));
  clamp_scroll_viewport(state, visible);
  wake_console_panel(layout_state, "problems.scroll");
  return true;
}

bool handle_problems_scrollbar_mouse(DiagnosticsPanelState* state, MainLayoutState* layout_state,
                                     const Mouse& m, int total, int visible_rows) {
  if (state == nullptr) {
    return false;
  }

  const int max_first = std::max(0, total - visible_rows);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);
  const bool scrollable = state->scrollbar_layout.scrollable;

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kProblemsScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kProblemsScrollbar; });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (state->scrollbar_dragging && scrollable) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->first_visible =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      return true;
    }
    return false;
  }

  if (state->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && scrollable) {
      const int local_y = m.y - state->scrollbar_box.y_min;
      const int thumb_top = local_y - state->scrollbar_drag_offset;
      state->first_visible =
          std::max(0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      return true;
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    return scroll_problems_by_delta(state, layout_state, -3);
  }
  if (m.button == Mouse::WheelDown) {
    return scroll_problems_by_delta(state, layout_state, 3);
  }

  if (!scrollable) {
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kProblemsScrollbar);
    const int local_y = m.y - state->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(state->scrollbar_layout, state->scrollbar_box, m.x, m.y)) {
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = local_y - state->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top = local_y - state->scrollbar_layout.thumb_height / 2;
      state->first_visible = std::max(
          0, std::min(scroll_for_thumb_top(state->scrollbar_layout, thumb_top), max_first));
      state->scrollbar_dragging = true;
      state->scrollbar_drag_offset = state->scrollbar_layout.thumb_height / 2;
    }
    return true;
  }

  return false;
}

void ensure_selection_visible(DiagnosticsPanelState* state, int visible_terminal_lines) {
  clamp_scroll_viewport(state, visible_terminal_lines);
  const int visible_rows = visible_problem_count(visible_terminal_lines);
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_rows) {
    state->first_visible = state->selected - visible_rows + 1;
  }
}

}  // namespace

Component MakeDiagnosticsPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                               std::shared_ptr<ISymbolProvider> symbols,
                               MainLayoutState* layout_state, WorkspaceIndexer* indexer,
                               DebugModel* model, SymbolWorkspaceIndexer* symbol_indexer) {
  auto state = std::make_shared<DiagnosticsPanelState>();

  auto renderer = Renderer([workspace, state, symbols, layout_state, indexer] {
    if (symbols && symbols->supports_diagnostics()) {
      const uint64_t revision = symbols->diagnostics_revision();
      const uint64_t view_token =
          workspace != nullptr ? workspace->buffer.view_token : static_cast<uint64_t>(0);
      const uint64_t cache_key = revision ^ (view_token << 1);
      if (cache_key != state->rows_revision) {
        state->rows = build_rows(workspace, symbols, indexer, layout_state);
        state->rows_revision = cache_key;
        if (state->selected >= static_cast<int>(state->rows.size())) {
          state->selected = std::max(0, static_cast<int>(state->rows.size()) - 1);
        }
      }
    } else {
      state->rows.clear();
      state->rows_revision = 0;
    }

    state->fix_boxes.assign(state->rows.size(), Box{});

    const int visible = problems_visible_terminal_lines(
        state->content_box, layout_state, static_cast<int>(state->rows.size()),
        state->last_visible_lines);
    state->last_visible_lines = visible;
    int panel_width = panel_content_width(state->list_content_box);
    if (!content_box_laid_out(state->list_content_box)) {
      panel_width = std::max(1, panel_content_width(state->content_box) - 1);
    }
    clamp_scroll_viewport(state.get(), visible);

    const bool lsp_fix_capable = code_actions_available(symbols, layout_state);
    const int visible_rows = visible_problem_count(visible);
    const int end =
        std::min(static_cast<int>(state->rows.size()), state->first_visible + visible_rows);
    if (!state->rows.empty() &&
        probe_visible_fix_availability(state.get(), workspace, symbols, layout_state,
                                        state->first_visible, end)) {
      if (layout_state != nullptr) {
        wake_console_panel(layout_state, "problems.fix_probe");
      }
    }

    Elements rows;
    if (!symbols || !symbols->supports_diagnostics()) {
      rows.push_back(text(i18n::tr("panel.problems.requires_clangd")) | color(theme::Muted()));
    } else if (state->rows.empty()) {
      rows.push_back(text(i18n::tr("panel.problems.no_problems")) | color(theme::Muted()));
    } else {
      const std::string workspace_root = workspace != nullptr ? workspace->root : std::string{};
      for (int i = state->first_visible; i < end; ++i) {
        const auto& row = state->rows[static_cast<std::size_t>(i)];
        const bool show_fix = lsp_fix_capable && row.fix == FixAvailability::kAvailable;
        const std::string fix_id = press_id::problems_fix(i);
        const bool fix_hovered =
            show_fix && layout_state != nullptr && layout_state->clickable.is_hovered(fix_id);
        const bool fix_pressed =
            show_fix && layout_state != nullptr && layout_state->clickable.is_pressed(fix_id);
        rows.push_back(make_problem_row_element(
            row, workspace_root, panel_width, i == state->selected, show_fix, fix_hovered,
            fix_pressed, show_fix ? &state->fix_boxes[static_cast<std::size_t>(i)] : nullptr));
      }
    }

    rows.push_back(filler());
    const int total = static_cast<int>(state->rows.size());
    state->scrollbar_layout =
        compute_scrollbar_layout(total, state->first_visible, visible_rows, visible);
    const bool scrollbar_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kProblemsScrollbar);
    const bool scrollbar_active =
        state->scrollbar_dragging ||
        (layout_state != nullptr &&
         layout_state->clickable.is_pressed(press_id::kProblemsScrollbar));
    Element scrollbar =
        vertical_scrollbar(total, state->first_visible, visible_rows, visible, scrollbar_hovered,
                           scrollbar_active) |
        reflect(state->scrollbar_box);
    Element list_body = vbox(std::move(rows)) | flex | reflect(state->list_content_box);
    return hbox({std::move(list_body) | flex, std::move(scrollbar)}) | flex |
           reflect(state->content_box) | bgcolor(theme::PanelBg());
  });

  auto handler = [workspace, focus, state, layout_state, symbols, model, symbol_indexer](Event event) {
    if (layout_state == nullptr || !problems_tab_active(layout_state)) {
      return false;
    }

    if (event.is_mouse()) {
      const auto& m = event.mouse();
      const int total = static_cast<int>(state->rows.size());
      const int visible = problems_visible_terminal_lines(
          state->content_box, layout_state, total, state->last_visible_lines);
      const int visible_rows = visible_problem_count(visible);

      if (handle_problems_scrollbar_mouse(state.get(), layout_state, m, total, visible_rows)) {
        wake_console_panel(layout_state, "problems.scrollbar");
        return true;
      }

      if (m.motion == Mouse::Moved) {
        if (!code_actions_available(symbols, layout_state) || state->fix_boxes.empty()) {
          return false;
        }
        if (layout_state == nullptr || !chrome_hover_allowed(layout_state)) {
          return false;
        }
        std::optional<std::string> hit_id;
        for (int i = 0; i < static_cast<int>(state->fix_boxes.size()); ++i) {
          if (i >= static_cast<int>(state->rows.size()) ||
              state->rows[static_cast<std::size_t>(i)].fix != FixAvailability::kAvailable) {
            continue;
          }
          const Box& box = state->fix_boxes[static_cast<std::size_t>(i)];
          if (!box.IsEmpty() && box.Contain(m.x, m.y)) {
            hit_id = press_id::problems_fix(i);
            break;
          }
        }
        if (hit_id.has_value()) {
          const std::string_view before = layout_state->clickable.hovered_id();
          layout_state->clickable.set_hover(*hit_id);
          return apply_hover_repaint(layout_state, before);
        }
        if (state->content_box.Contain(m.x, m.y) ||
            press_id::is_problems_hover(layout_state->clickable.hovered_id())) {
          const std::string_view before = layout_state->clickable.hovered_id();
          layout_state->clickable.clear_hover_if(press_id::is_problems_hover);
          return apply_hover_repaint(layout_state, before);
        }
        return false;
      }

      if ((state->content_box.Contain(m.x, m.y) || state->scrollbar_box.Contain(m.x, m.y) ||
           state->list_content_box.Contain(m.x, m.y)) &&
          (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown)) {
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        const int delta = m.button == Mouse::WheelUp ? -3 : 3;
        return scroll_problems_by_delta(state.get(), layout_state, delta);
      }
      if (m.button == Mouse::Right && m.motion == Mouse::Pressed) {
        if (!state->list_content_box.Contain(m.x, m.y)) {
          return false;
        }
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        const int visible = visible_line_count(state->list_content_box);
        const int visual_row = m.y - state->list_content_box.y_min;
        const int row = state->first_visible + (visual_row / kLinesPerProblem);
        if (row < 0 || row >= static_cast<int>(state->rows.size())) {
          return false;
        }
        state->selected = row;
        ensure_selection_visible(state.get(), visible);
        const DiagnosticRow& diag = state->rows[static_cast<std::size_t>(row)];
        const bool lsp_available = code_actions_available(symbols, layout_state);
        context_menu_open_problem(&layout_state->context_menu, m.x, m.y, diag.path, diag.line,
                                  diag.character, diag.end_col, diag.message, lsp_available);
        wake_console_panel(layout_state, "problems.menu");
        return true;
      }
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (!state->list_content_box.Contain(m.x, m.y)) {
        return false;
      }

      if (code_actions_available(symbols, layout_state)) {
        for (int i = 0; i < static_cast<int>(state->fix_boxes.size()); ++i) {
          if (i >= static_cast<int>(state->rows.size()) ||
              state->rows[static_cast<std::size_t>(i)].fix != FixAvailability::kAvailable) {
            continue;
          }
          const Box& box = state->fix_boxes[static_cast<std::size_t>(i)];
          if (box.IsEmpty() || !box.Contain(m.x, m.y)) {
            continue;
          }
          state->selected = i;
          trigger_press(layout_state, press_id::problems_fix(i));
          apply_fix_for_row(workspace, model, layout_state, symbols, symbol_indexer,
                             state->rows[static_cast<std::size_t>(i)]);
          if (focus != nullptr) {
            focus->region = FocusRegion::Editor;
          }
          if (layout_state != nullptr) {
            wake_console_panel(layout_state, "problems.fix");
          }
          return true;
        }
      }

      const int visible = visible_line_count(state->list_content_box);
      const int visual_row = m.y - state->list_content_box.y_min;
      const int row = state->first_visible + (visual_row / kLinesPerProblem);
      if (row >= 0 && row < static_cast<int>(state->rows.size())) {
        state->selected = row;
        ensure_selection_visible(state.get(), visible);
        navigate_to_diagnostic(workspace, state->rows[static_cast<std::size_t>(row)]);
        if (focus != nullptr) {
          focus->region = FocusRegion::Editor;
        }
        wake_console_panel(layout_state, "problems.navigate");
        return true;
      }
      return false;
    }

    if (layout_state != nullptr &&
        is_editor_chrome_input_focus(layout_state->text_input_focus)) {
      return false;
    }

    if (state->rows.empty()) {
      return false;
    }

    const int visible = visible_line_count(state->content_box);
    const int visible_rows = visible_problem_count(visible);
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      state->selected =
          std::min(state->selected + 1, static_cast<int>(state->rows.size()) - 1);
      ensure_selection_visible(state.get(), visible);
      wake_console_panel(layout_state, "problems.select");
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      ensure_selection_visible(state.get(), visible);
      wake_console_panel(layout_state, "problems.select");
      return true;
    }
    if (event == Event::Return) {
      navigate_to_diagnostic(workspace, state->rows[static_cast<std::size_t>(state->selected)]);
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      return true;
    }
    if (event == Event::Character('f') && code_actions_available(symbols, layout_state)) {
      const DiagnosticRow& row = state->rows[static_cast<std::size_t>(state->selected)];
      if (row.fix != FixAvailability::kAvailable) {
        return false;
      }
      apply_fix_for_row(workspace, model, layout_state, symbols, symbol_indexer, row);
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      wake_console_panel(layout_state, "problems.fix");
      return true;
    }
    if (event == Event::PageDown) {
      state->selected =
          std::min(state->selected + visible_rows, static_cast<int>(state->rows.size()) - 1);
      ensure_selection_visible(state.get(), visible);
      wake_console_panel(layout_state, "problems.select");
      return true;
    }
    if (event == Event::PageUp) {
      state->selected = std::max(0, state->selected - visible_rows);
      ensure_selection_visible(state.get(), visible);
      wake_console_panel(layout_state, "problems.select");
      return true;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->problems_key_handler = handler;
    layout_state->problems_scroll_handler = [state, layout_state](int delta) {
      return scroll_problems_by_delta(state.get(), layout_state, delta);
    };
  }

  return WrapFocusable(CatchEvent(renderer, handler));
}

}  // namespace tuide
