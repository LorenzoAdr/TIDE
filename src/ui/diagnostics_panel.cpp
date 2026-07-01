#include "ui/diagnostics_panel.hpp"

#include <algorithm>
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
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;
namespace fs = std::filesystem;

namespace {

struct DiagnosticRow {
  std::string path;
  int line = 0;
  int character = 0;
  std::string message;
  DiagnosticSeverity severity = DiagnosticSeverity::kError;
};

struct DiagnosticsPanelState {
  std::vector<DiagnosticRow> rows;
  int selected = 0;
  int first_visible = 0;
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
  workspace->status_message =
      "→ " + fs::path(row.path).filename().string() + ":" + std::to_string(row.line + 1) + ":" +
      std::to_string(row.character + 1);
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

void clamp_scroll(DiagnosticsPanelState* state, int visible_lines) {
  const int total = static_cast<int>(state->rows.size());
  const int max_first = std::max(0, total - visible_lines);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_lines) {
    state->first_visible = state->selected - visible_lines + 1;
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
    clamp_scroll(state.get(), visible);

    Elements rows;
    if (!symbols || !symbols->supports_diagnostics()) {
      rows.push_back(text(" (requiere clangd) ") | color(theme::Muted()));
    } else if (state->rows.empty()) {
      rows.push_back(text(" (sin problemas) ") | color(theme::Muted()));
    } else {
      const std::string workspace_root = workspace != nullptr ? workspace->root : std::string{};
      const int end = std::min(static_cast<int>(state->rows.size()), state->first_visible + visible);
      for (int i = state->first_visible; i < end; ++i) {
        const auto& row = state->rows[static_cast<std::size_t>(i)];
        const std::string loc = display_path(row.path, workspace_root) + ":" +
                                std::to_string(row.line + 1) + " ";
        const char marker = severity_marker(row.severity);
        std::string line = std::string(1, marker) + " " + loc + row.message;
        Element el = text(" " + line);
        if (i == state->selected) {
          el = el | inverted | bold;
        } else {
          el = el | color(severity_color(row.severity));
        }
        rows.push_back(std::move(el));
      }
    }

    return vbox(std::move(rows)) | vscroll_indicator | frame | flex |
           reflect(state->content_box) | bgcolor(theme::PanelBg());
  });

  auto handler = [workspace, focus, state, layout_state](Event event) {
    if (layout_state == nullptr || !problems_tab_active(layout_state)) {
      return false;
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
      const int row = visual_row + state->first_visible;
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

    if (state->rows.empty()) {
      return false;
    }

    const int visible = visible_line_count(state->content_box);
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
          std::min(state->selected + visible, static_cast<int>(state->rows.size()) - 1);
      clamp_scroll(state.get(), visible);
      return true;
    }
    if (event == Event::PageUp) {
      state->selected = std::max(0, state->selected - visible);
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
