#include "ui/search_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include "app/debug_model.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "search/workspace_search.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kResultVisualRows = 2;

Element highlight_preview(const std::string& line, const std::string& needle, bool selected) {
  const std::string indent = "   ";
  if (needle.empty()) {
    Element row = text(indent + line) | color(theme::Header());
    if (selected) {
      row = row | inverted;
    }
    return row;
  }

  Elements parts;
  parts.push_back(text(indent));
  std::size_t pos = 0;
  while (pos <= line.size()) {
    const auto found = line.find(needle, pos);
    if (found == std::string::npos) {
      parts.push_back(text(line.substr(pos)) | color(theme::Header()));
      break;
    }
    if (found > pos) {
      parts.push_back(text(line.substr(pos, found - pos)) | color(theme::Header()));
    }
    parts.push_back(text(needle) | color(theme::Stop()) | bold);
    pos = found + needle.size();
  }

  Element row = hbox(std::move(parts));
  if (selected) {
    row = row | inverted;
  }
  return row;
}

Component make_input_row(Component input) {
  return Container::Horizontal({std::move(input) | flex});
}

Element render_search_field(const std::string& label, const std::string& value,
                            const std::string& placeholder, bool active, Component row,
                            Box* box) {
  Element field;
  if (active) {
    field = row->Render() | flex | border | bgcolor(theme::PanelBg()) | size(HEIGHT, EQUAL, 3);
  } else {
    const std::string& preview = value.empty() ? placeholder : value;
    field = ModalInputLine(preview) | flex;
    if (value.empty()) {
      field = field | dim;
    }
  }

  return hbox({
             text(" " + label + " ") | color(theme::Muted()) |
                 size(WIDTH, EQUAL, static_cast<int>(label.size()) + 2),
             std::move(field) | flex | reflect(*box),
         }) |
         size(HEIGHT, EQUAL, active ? 3 : 1);
}

}  // namespace

struct SearchPanelState {
  std::string query;
  std::string replace;
  std::string path_filter;
  std::string exclude_pattern;
  std::vector<WorkspaceSearchResult> results;
  int selected = 0;
  Box results_box;
  Box query_box;
  Box replace_box;
  Box path_box;
  Box exclude_box;
  std::string status;
  int result_count = 0;
};

void clear_search_input_focus(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  if (is_search_input_focus(layout_state->text_input_focus)) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
}

WorkspaceSearchOptions build_options(SearchPanelState* state, DebugModel* model,
                                     WorkspaceIndexer* indexer) {
  WorkspaceSearchOptions opts;
  opts.needle = state->query;
  opts.path_filter = state->path_filter;
  opts.exclude_pattern = state->exclude_pattern;
  opts.workspace_root = model != nullptr ? model->workspace_root : std::string{};
  if (indexer != nullptr) {
    if (auto snap = indexer->snapshot()) {
      if (snap->workspace_root == opts.workspace_root) {
        opts.files = snap->files;
      }
    }
  }
  return opts;
}

void run_search(SearchPanelState* state, DebugModel* model, WorkspaceIndexer* indexer) {
  if (state->query.empty()) {
    state->results.clear();
    state->selected = 0;
    state->result_count = 0;
    state->status = "Introduce un texto para buscar";
    return;
  }
  const auto opts = build_options(state, model, indexer);
  state->results = search_workspace(opts);
  state->result_count = static_cast<int>(state->results.size());
  state->selected = 0;
  if (state->results.empty()) {
    state->status = "Sin coincidencias";
  } else {
    state->status = std::to_string(state->result_count) + " coincidencia(s)";
  }
}

void run_replace_all(SearchPanelState* state, DebugModel* model, WorkspaceIndexer* indexer,
                     WorkspaceModel* workspace) {
  if (state->query.empty() || state->replace.empty()) {
    state->status = "Necesitas texto de búsqueda y reemplazo";
    return;
  }
  const auto opts = build_options(state, model, indexer);
  const auto result = replace_in_workspace(opts, state->replace);
  run_search(state, model, indexer);
  state->status = "Reemplazadas " + std::to_string(result.replacements) +
                  " en " + std::to_string(result.files_modified) + " archivo(s)";

  if (workspace != nullptr && !workspace->buffer.path.empty()) {
    namespace fs = std::filesystem;
    for (const auto& rel : opts.files) {
      std::error_code ec;
      const auto absolute = fs::weakly_canonical(fs::path(opts.workspace_root) / rel, ec);
      if (!ec && absolute.string() == workspace->buffer.path) {
        workspace->open_file(workspace->buffer.path);
        break;
      }
    }
  }
}

void open_result(SearchPanelState* state, WorkspaceModel* workspace, DebugModel* model,
                 FocusManagerState* focus, int index) {
  if (state->results.empty()) {
    return;
  }
  index = std::max(0, std::min(index, static_cast<int>(state->results.size()) - 1));
  const auto& hit = state->results[static_cast<std::size_t>(index)];
  if (model != nullptr) {
    model->active_file = hit.file;
    model->active_line = hit.line;
    model->view_token++;
  }
  if (workspace != nullptr && model != nullptr && !model->workspace_root.empty()) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto absolute =
        fs::weakly_canonical(fs::path(model->workspace_root) / hit.file, ec);
    if (!ec) {
      workspace->record_cursor_jump();
      workspace->open_file_at(absolute.string(), std::max(0, hit.line - 1),
                              std::max(0, hit.col - 1));
    }
  }
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
}

Component MakeSearchPanel(WorkspaceModel* workspace, DebugModel* model,
                          FocusManagerState* focus, MainLayoutState* layout_state,
                          WorkspaceIndexer* indexer, RightSidebarState* sidebar) {
  auto state = std::make_shared<SearchPanelState>();

  auto query_input = Input(MakeBlinkInputOption(&state->query, "buscar..."));
  auto replace_input = Input(MakeBlinkInputOption(&state->replace, "reemplazar..."));
  auto path_input = Input(MakeBlinkInputOption(&state->path_filter, "todo el workspace"));
  auto exclude_input = Input(MakeBlinkInputOption(&state->exclude_pattern, "p. ej. *.h"));

  auto query_row = make_input_row(query_input);
  auto replace_row = make_input_row(replace_input);
  auto path_row = make_input_row(path_input);
  auto exclude_row = make_input_row(exclude_input);

  auto query_active = Maybe(query_row, [layout_state] {
    return layout_state &&
           layout_state->text_input_focus == TextInputFocus::SearchQuery;
  });
  auto replace_active = Maybe(replace_row, [layout_state] {
    return layout_state &&
           layout_state->text_input_focus == TextInputFocus::SearchReplace;
  });
  auto path_active = Maybe(path_row, [layout_state] {
    return layout_state && layout_state->text_input_focus == TextInputFocus::SearchPath;
  });
  auto exclude_active = Maybe(exclude_row, [layout_state] {
    return layout_state &&
           layout_state->text_input_focus == TextInputFocus::SearchExclude;
  });

  auto input_layers = Container::Vertical(
      {query_active, replace_active, path_active, exclude_active});

  auto activate_field = [layout_state, focus](TextInputFocus field, Component input) {
    if (focus != nullptr) {
      focus->region = FocusRegion::RightPanel;
    }
    if (layout_state) {
      layout_state->text_input_focus = field;
    }
    input->TakeFocus();
  };

  auto handler = [state, workspace, model, focus, layout_state, indexer, sidebar, query_input,
                  replace_input, path_input, exclude_input, activate_field](Event event) {
    if (event == Event::Custom && sidebar != nullptr && sidebar->pending_search_setup) {
      state->query = sidebar->pending_search_query;
      state->path_filter = sidebar->pending_search_path_filter;
      sidebar->pending_search_setup = false;
      sidebar->pending_search_query.clear();
      sidebar->pending_search_path_filter.clear();
      run_search(state.get(), model, indexer);
    }
    if (event == Event::Custom && sidebar != nullptr && sidebar->pending_focus_search) {
      sidebar->pending_focus_search = false;
      activate_field(TextInputFocus::SearchQuery, query_input);
      return false;
    }

    if (sidebar == nullptr || sidebar->selected_tab != RightSidebarTabs::kSearch) {
      return false;
    }

    if (event == Event::Escape) {
      clear_search_input_focus(layout_state);
      return true;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      focus->region = FocusRegion::RightPanel;
      if (state->query_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchQuery, query_input);
        return false;
      }
      if (state->replace_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchReplace, replace_input);
        return false;
      }
      if (state->path_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchPath, path_input);
        return false;
      }
      if (state->exclude_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchExclude, exclude_input);
        return false;
      }
      if (state->results_box.Contain(m.x, m.y)) {
        clear_search_input_focus(layout_state);
        const int visual_row = m.y - state->results_box.y_min;
        const int result_index = visual_row / kResultVisualRows;
        if (result_index >= 0 && result_index < static_cast<int>(state->results.size())) {
          state->selected = result_index;
          open_result(state.get(), workspace, model, focus, result_index);
        }
        return true;
      }
      return false;
    }

    if (focus->region != FocusRegion::RightPanel &&
        !is_search_input_focus(layout_state != nullptr ? layout_state->text_input_focus
                                                       : TextInputFocus::None)) {
      return false;
    }

    const bool in_input =
        layout_state && is_search_input_focus(layout_state->text_input_focus);

    if (in_input) {
      if (event == Event::Tab) {
        switch (layout_state->text_input_focus) {
          case TextInputFocus::SearchQuery:
            activate_field(TextInputFocus::SearchReplace, replace_input);
            break;
          case TextInputFocus::SearchReplace:
            activate_field(TextInputFocus::SearchPath, path_input);
            break;
          case TextInputFocus::SearchPath:
            activate_field(TextInputFocus::SearchExclude, exclude_input);
            break;
          default:
            activate_field(TextInputFocus::SearchQuery, query_input);
            break;
        }
        return true;
      }
      if (event == Event::Return) {
        run_search(state.get(), model, indexer);
        clear_search_input_focus(layout_state);
        return true;
      }
      if (event == Event::Character('r') || event == Event::Character('R')) {
        if (!state->replace.empty()) {
          run_replace_all(state.get(), model, indexer, workspace);
          return true;
        }
      }
      return false;
    }

    if (event == Event::Return) {
      if (!state->results.empty()) {
        open_result(state.get(), workspace, model, focus, state->selected);
        return true;
      }
      run_search(state.get(), model, indexer);
      activate_field(TextInputFocus::SearchQuery, query_input);
      return true;
    }
    if (event == Event::Character('r') || event == Event::Character('R')) {
      if (!state->replace.empty()) {
        run_replace_all(state.get(), model, indexer, workspace);
        return true;
      }
      return false;
    }
    if (event == Event::Character('/')) {
      activate_field(TextInputFocus::SearchQuery, query_input);
      return true;
    }

    if (event == Event::ArrowDown || event == Event::Character('j')) {
      if (!state->results.empty()) {
        state->selected =
            std::min(state->selected + 1, static_cast<int>(state->results.size()) - 1);
        return true;
      }
      return false;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      return true;
    }

    return false;
  };

  if (layout_state != nullptr) {
    layout_state->search_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(
      Renderer(input_layers, [state, layout_state, query_row, replace_row, path_row, exclude_row,
                              focus] {
        const auto active = layout_state != nullptr ? layout_state->text_input_focus
                                                    : TextInputFocus::None;

        Element form = vbox({
            render_search_field("Buscar", state->query, "buscar...",
                                active == TextInputFocus::SearchQuery, query_row,
                                &state->query_box),
            render_search_field("Reempl", state->replace, "reemplazar...",
                                active == TextInputFocus::SearchReplace, replace_row,
                                &state->replace_box),
            render_search_field("Ruta", state->path_filter, "todo el workspace",
                                active == TextInputFocus::SearchPath, path_row, &state->path_box),
            render_search_field("Excl", state->exclude_pattern, "p. ej. *.h",
                                active == TextInputFocus::SearchExclude, exclude_row,
                                &state->exclude_box),
            separator(),
            text(" " + state->status +
                 (state->replace.empty() ? "  Enter: buscar" : "  Enter: buscar  R: reemplazar")) |
                color(theme::Muted()) | size(HEIGHT, EQUAL, 1),
        });

        Elements rows;
        if (state->results.empty() && state->query.empty()) {
          rows.push_back(text("(sin resultados)") | color(theme::Muted()));
        } else if (state->results.empty()) {
          rows.push_back(text("(sin coincidencias)") | color(theme::Muted()));
        } else {
          for (int i = 0; i < static_cast<int>(state->results.size()); ++i) {
            const auto& hit = state->results[static_cast<std::size_t>(i)];
            const std::string location = hit.file + ":" + std::to_string(hit.line);
            const bool selected =
                i == state->selected && focus->region == FocusRegion::RightPanel;

            Element file_line = text(" " + location) | color(theme::Accent());
            if (selected) {
              file_line = file_line | inverted | bold;
            }
            Element preview_line = highlight_preview(hit.preview, state->query, selected);
            rows.push_back(vbox({std::move(file_line), std::move(preview_line)}));
          }
        }

        auto results = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                       reflect(state->results_box) | bgcolor(theme::PanelBg());

        return PanelBody(vbox({std::move(form), std::move(results)}));
      }),
      handler));
}

}  // namespace tgdb
