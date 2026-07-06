#include "ui/diagnostics_panel.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <system_error>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "lsp/diagnostics.hpp"
#include "editor/editor_state.hpp"
#include "ui/focus_manager.hpp"
#include "ui/focusable_component.hpp"
#include "ui/context_menu.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

struct DiagnosticRow {
  std::string path;
  int line = 0;
  int character = 0;
  int end_col = 0;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::kError;
};

struct DiagnosticsPanelState {
  std::vector<DiagnosticRow> rows;
  int selected = 0;
  int first_visible = 0;
  int last_visible_lines = 1;
  Box content_box;
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

std::string buffer_text(const EditorBuffer& buffer) {
  std::string text;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    if (i > 0) {
      text.push_back('\n');
    }
    text += buffer.lines[i];
  }
  return text;
}

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
                                      WorkspaceIndexer* indexer) {
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
  if (!diagnostics_display_allowed(last_edit_ms, symbols.get(), active)) {
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

Element make_problem_row_element(const DiagnosticRow& row, const std::string& workspace_root,
                                 int panel_width, bool selected) {
  const auto wrapped = wrap_problem_line(format_problem_line(row, workspace_root), panel_width);
  Element first = text(wrapped[0]) | size(HEIGHT, EQUAL, 1);
  Element second = text(wrapped[1]) | size(HEIGHT, EQUAL, 1);
  Element row_el = vbox({std::move(first), std::move(second)});
  if (selected) {
    row_el = row_el | inverted | bold;
  } else {
    row_el = row_el | color(severity_color(row.severity));
  }
  return row_el;
}

void clamp_scroll(DiagnosticsPanelState* state, int visible_terminal_lines) {
  const int total = static_cast<int>(state->rows.size());
  const int visible_rows = visible_problem_count(visible_terminal_lines);
  const int max_first = std::max(0, total - visible_rows);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_rows) {
    state->first_visible = state->selected - visible_rows + 1;
  }
}

}  // namespace

Component MakeDiagnosticsPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                 std::shared_ptr<ISymbolProvider> symbols,
                                 MainLayoutState* layout_state, WorkspaceIndexer* indexer) {
  auto state = std::make_shared<DiagnosticsPanelState>();

  auto renderer = Renderer([workspace, state, symbols, layout_state, indexer] {
    if (symbols && symbols->supports_diagnostics()) {
      const uint64_t revision = symbols->diagnostics_revision();
      const uint64_t view_token =
          workspace != nullptr ? workspace->buffer.view_token : static_cast<uint64_t>(0);
      const uint64_t cache_key = revision ^ (view_token << 1);
      if (cache_key != state->rows_revision) {
        state->rows = build_rows(workspace, symbols, indexer);
        state->rows_revision = cache_key;
        if (state->selected >= static_cast<int>(state->rows.size())) {
          state->selected = std::max(0, static_cast<int>(state->rows.size()) - 1);
        }
      }
    } else {
      state->rows.clear();
      state->rows_revision = 0;
    }

    const int visible = visible_line_count(state->content_box);
    state->last_visible_lines = visible;
    const int panel_width = panel_content_width(state->content_box);
    clamp_scroll(state.get(), visible);

    Elements rows;
    if (!symbols || !symbols->supports_diagnostics()) {
      rows.push_back(text(i18n::tr("panel.problems.requires_clangd")) | color(theme::Muted()));
    } else if (state->rows.empty()) {
      rows.push_back(text(i18n::tr("panel.problems.no_problems")) | color(theme::Muted()));
    } else {
      const std::string workspace_root = workspace != nullptr ? workspace->root : std::string{};
      const int visible_rows = visible_problem_count(visible);
      const int end =
          std::min(static_cast<int>(state->rows.size()), state->first_visible + visible_rows);
      for (int i = state->first_visible; i < end; ++i) {
        const auto& row = state->rows[static_cast<std::size_t>(i)];
        rows.push_back(make_problem_row_element(row, workspace_root, panel_width, i == state->selected));
      }
    }

    return vbox(std::move(rows)) | vscroll_indicator | frame | flex |
           reflect(state->content_box) | bgcolor(theme::PanelBg());
  });

  auto handler = [workspace, focus, state, layout_state, symbols](Event event) {
    if (layout_state == nullptr || !problems_tab_active(layout_state)) {
      return false;
    }

    if (event.is_mouse()) {
      const auto& m = event.mouse();
      if (state->content_box.Contain(m.x, m.y) &&
          (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown)) {
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        const int visible = visible_line_count(state->content_box);
        const int visible_rows = visible_problem_count(visible);
        const int total = static_cast<int>(state->rows.size());
        const int max_first = std::max(0, total - visible_rows);
        if (m.button == Mouse::WheelUp) {
          state->first_visible = std::max(0, state->first_visible - 1);
        } else {
          state->first_visible = std::min(state->first_visible + 1, max_first);
        }
        clamp_scroll(state.get(), visible);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
        return true;
      }
      if (m.button == Mouse::Right && m.motion == Mouse::Pressed) {
        if (!state->content_box.Contain(m.x, m.y)) {
          return false;
        }
        if (focus != nullptr) {
          focus->region = FocusRegion::Terminal;
        }
        const int visible = visible_line_count(state->content_box);
        const int visual_row = m.y - state->content_box.y_min;
        const int row = state->first_visible + (visual_row / kLinesPerProblem);
        if (row < 0 || row >= static_cast<int>(state->rows.size())) {
          return false;
        }
        state->selected = row;
        clamp_scroll(state.get(), visible);
        const DiagnosticRow& diag = state->rows[static_cast<std::size_t>(row)];
        const bool lsp_available =
            symbols != nullptr && symbols->supports_code_actions() &&
            (layout_state->app_settings == nullptr || layout_state->app_settings->lsp_enabled);
        context_menu_open_problem(&layout_state->context_menu, m.x, m.y, diag.path, diag.line,
                                  diag.character, diag.end_col, diag.message, lsp_available);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
        return true;
      }
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      if (!state->content_box.Contain(m.x, m.y)) {
        return false;
      }
      const int visible = visible_line_count(state->content_box);
      const int visual_row = m.y - state->content_box.y_min;
      const int row = state->first_visible + (visual_row / kLinesPerProblem);
      if (row >= 0 && row < static_cast<int>(state->rows.size())) {
        state->selected = row;
        clamp_scroll(state.get(), visible);
        navigate_to_diagnostic(workspace, state->rows[static_cast<std::size_t>(row)]);
        if (focus != nullptr) {
          focus->region = FocusRegion::Editor;
        }
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
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
      clamp_scroll(state.get(), visible);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      clamp_scroll(state.get(), visible);
      return true;
    }
    if (event == Event::Return) {
      navigate_to_diagnostic(workspace, state->rows[static_cast<std::size_t>(state->selected)]);
      if (focus != nullptr) {
        focus->region = FocusRegion::Editor;
      }
      return true;
    }
    if (event == Event::PageDown) {
      state->selected =
          std::min(state->selected + visible_rows, static_cast<int>(state->rows.size()) - 1);
      clamp_scroll(state.get(), visible);
      return true;
    }
    if (event == Event::PageUp) {
      state->selected = std::max(0, state->selected - visible_rows);
      clamp_scroll(state.get(), visible);
      return true;
    }
    return false;
  };

  if (layout_state != nullptr) {
    layout_state->problems_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(renderer, handler));
}

}  // namespace tgdb
