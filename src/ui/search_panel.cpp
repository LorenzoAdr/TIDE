#include "ui/search_panel.hpp"
#include "ui/ui_wake.hpp"

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
#include "indexer/workspace_indexer.hpp"
#include "search/workspace_search.hpp"
#include "search/workspace_search_runner.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"
#include "ui/key_bindings.hpp"
#include "i18n/tr.hpp"

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kLabelWidth = 8;

Element render_search_field(const std::string& label, const std::string& value,
                            const std::string& placeholder, bool active, Component input,
                            Box* box) {
  Element field;
  if (active) {
    field = input->Render() | flex | border | bgcolor(theme::PanelBg());
  } else {
    const std::string& preview = value.empty() ? placeholder : value;
    field = ModalInputLine(preview) | flex;
    if (value.empty()) {
      field = field | dim;
    }
  }

  return hbox({
             text(label) | color(theme::Muted()) | size(WIDTH, EQUAL, kLabelWidth),
             std::move(field) | flex | reflect(*box),
         }) |
         flex;
}

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

}  // namespace

struct SearchPanelState {
  std::string query;
  std::string committed_query;
  std::string replace;
  std::string path_filter;
  std::string include_pattern;
  std::string exclude_pattern;
  std::string placeholder_query;
  std::string placeholder_replace;
  std::string placeholder_path;
  std::string placeholder_include;
  std::string placeholder_exclude;
  std::vector<WorkspaceSearchResult> results;
  int selected = 0;
  int first_visible = 0;
  int last_visible_lines = 1;
  Box results_box;
  Box query_box;
  Box replace_box;
  Box path_box;
  Box include_box;
  Box exclude_box;
  std::string status;
  int result_count = 0;
  WorkspaceSearchRunner runner;
  uint64_t search_generation = 0;
};

namespace {

void clamp_search_scroll(SearchPanelState* state, int visible_lines) {
  if (state == nullptr) {
    return;
  }
  const int total = static_cast<int>(state->results.size());
  const int max_first = std::max(0, total - visible_lines);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_lines) {
    state->first_visible = state->selected - visible_lines + 1;
  }
}

}  // namespace

void clear_search_input_focus(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  if (is_search_input_focus(layout_state->text_input_focus)) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
}

WorkspaceSearchOptions build_options(SearchPanelState* state, WorkspaceModel* workspace,
                                     DebugModel* model, WorkspaceIndexer* indexer) {
  WorkspaceSearchOptions opts;
  opts.needle = state->query;
  opts.path_filter = state->path_filter;
  opts.include_pattern = state->include_pattern;
  opts.exclude_pattern = state->exclude_pattern;
  if (model != nullptr && !model->workspace_root.empty()) {
    opts.workspace_root = model->workspace_root;
  } else if (workspace != nullptr && !workspace->root.empty()) {
    opts.workspace_root = workspace->root;
  }
  if (!opts.workspace_root.empty()) {
    std::error_code ec;
    opts.workspace_root = std::filesystem::absolute(opts.workspace_root, ec).string();
  }
  if (indexer != nullptr) {
    if (auto snap = indexer->snapshot()) {
      if (snap->workspace_root == opts.workspace_root) {
        opts.files = snap->files;
      }
    }
  }
  if (opts.files.empty() && !opts.workspace_root.empty()) {
    opts.files = scan_workspace_files(opts.workspace_root);
  }
  return opts;
}

void apply_search_results(SearchPanelState* state, std::vector<WorkspaceSearchResult> results,
                          bool cancelled, int files_scanned, bool used_rg) {
  if (state == nullptr) {
    return;
  }
  state->results = std::move(results);
  state->result_count = static_cast<int>(state->results.size());
  state->selected = 0;
  state->first_visible = 0;
  const std::string backend = used_rg ? i18n::tr("search.status.backend.rg") : i18n::tr("search.status.backend.internal");
  if (state->results.empty()) {
    if (cancelled) {
      state->status = i18n::tr("search.status.cancelled");
    } else if (state->query.empty()) {
      state->status = i18n::tr("search.status.enter_query");
    } else {
      state->status = i18n::tr("search.status.no_matches") + backend;
    }
  } else {
    state->status = i18n::tr_fmt("search.status.match_count", {std::to_string(state->result_count)}) + backend;
    if (state->result_count >= 2000) {
      state->status += i18n::tr("search.status.limit_reached");
    }
  }
  if (files_scanned > 0 && state->results.empty() && !cancelled && !state->query.empty()) {
    state->status += i18n::tr_fmt("search.status.files_scanned", {std::to_string(files_scanned)});
  }
}

bool poll_search_results(SearchPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr) {
    return false;
  }
  std::vector<WorkspaceSearchResult> results;
  bool cancelled = false;
  int files_scanned = 0;
  bool used_rg = false;
  if (!state->runner.poll(&results, &cancelled, &files_scanned, &used_rg)) {
    return false;
  }
  apply_search_results(state, std::move(results), cancelled, files_scanned, used_rg);
  if (layout_state != nullptr) {
    UI_WAKE(layout_state, "wake");
  }
  return true;
}

void run_search(SearchPanelState* state, WorkspaceModel* workspace, DebugModel* model,
                WorkspaceIndexer* indexer, MainLayoutState* layout_state) {
  if (state->query.empty()) {
    state->runner.cancel();
    state->committed_query.clear();
    state->results.clear();
    state->selected = 0;
    state->result_count = 0;
    state->status = i18n::tr("search.status.enter_query");
    return;
  }
  state->committed_query = state->query;
  const auto opts = build_options(state, workspace, model, indexer);
  if (opts.workspace_root.empty()) {
    state->status = i18n::tr("search.status.no_workspace");
    return;
  }
  state->status = i18n::tr("search.status.searching");
  state->results.clear();
  state->selected = 0;
  state->first_visible = 0;
  state->result_count = 0;
  ++state->search_generation;
  state->runner.start(opts);
  if (layout_state != nullptr) {
    UI_WAKE(layout_state, "wake");
  }
}

void run_replace_all(SearchPanelState* state, WorkspaceModel* workspace, DebugModel* model,
                     WorkspaceIndexer* indexer, MainLayoutState* layout_state) {
  if (state->query.empty() || state->replace.empty()) {
    state->status = i18n::tr("search.status.need_replace");
    return;
  }
  state->runner.cancel();
  const auto opts = build_options(state, workspace, model, indexer);
  const auto result = replace_in_workspace(opts, state->replace);
  run_search(state, workspace, model, indexer, layout_state);
  state->status = i18n::tr_fmt("search.status.replaced", {std::to_string(result.replacements), std::to_string(result.files_modified)});

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
  if (workspace != nullptr && model != nullptr && !model->workspace_root.empty()) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto absolute =
        fs::weakly_canonical(fs::path(model->workspace_root) / hit.file, ec);
    if (!ec) {
      workspace->record_cursor_jump();
      if (!workspace->open_file_at(absolute.string(), std::max(0, hit.line - 1),
                                   std::max(0, hit.col - 1))) {
        return;
      }
    }
  }
  if (model != nullptr) {
    model->active_file = hit.file;
    model->active_line = hit.line;
    model->view_token++;
  }
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
}

Component MakeSearchPanel(WorkspaceModel* workspace, DebugModel* model,
                          FocusManagerState* focus, MainLayoutState* layout_state,
                          WorkspaceIndexer* indexer, RightSidebarState* sidebar) {
  auto state = std::make_shared<SearchPanelState>();
  state->placeholder_query = i18n::tr("search.placeholder.search");
  state->placeholder_replace = i18n::tr("search.placeholder.replace");
  state->placeholder_path = i18n::tr("search.placeholder.path");
  state->placeholder_include = i18n::tr("search.placeholder.include");
  state->placeholder_exclude = i18n::tr("search.placeholder.exclude");

  auto query_input = Input(MakeBlinkInputOption(&state->query, &state->placeholder_query));
  auto replace_input = Input(MakeBlinkInputOption(&state->replace, &state->placeholder_replace));
  auto path_input = Input(MakeBlinkInputOption(&state->path_filter, &state->placeholder_path));
  auto include_input =
      Input(MakeBlinkInputOption(&state->include_pattern, &state->placeholder_include));
  auto exclude_input =
      Input(MakeBlinkInputOption(&state->exclude_pattern, &state->placeholder_exclude));

  auto input_layers = Container::Horizontal(
      {query_input | flex, replace_input | flex, path_input | flex, include_input | flex,
       exclude_input | flex});

  auto activate_field = [layout_state, focus](TextInputFocus field, Component input) {
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
    if (layout_state != nullptr) {
      layout_state->text_input_focus = field;
    }
    input->TakeFocus();
  };

  auto active_search_input = [layout_state, query_input, replace_input, path_input, include_input,
                              exclude_input]() -> Component {
    if (layout_state == nullptr) {
      return query_input;
    }
    switch (layout_state->text_input_focus) {
      case TextInputFocus::SearchReplace:
        return replace_input;
      case TextInputFocus::SearchPath:
        return path_input;
      case TextInputFocus::SearchInclude:
        return include_input;
      case TextInputFocus::SearchExclude:
        return exclude_input;
      default:
        return query_input;
    }
  };

  auto forward_input_event = [active_search_input](Event event) {
    const Component input = active_search_input();
    return input != nullptr && input->OnEvent(event);
  };

  auto handler = [state, workspace, model, focus, layout_state, indexer, sidebar, query_input,
                  replace_input, path_input, include_input, exclude_input, activate_field,
                  forward_input_event](Event event) {
    if (event == Event::Custom) {
      poll_search_results(state.get(), layout_state);
      if (state->runner.running() && layout_state != nullptr) {
        UI_WAKE(layout_state, "wake");
      }
    }
    if (event == Event::Custom && sidebar != nullptr && sidebar->pending_search_setup) {
      state->query = sidebar->pending_search_query;
      state->path_filter = sidebar->pending_search_path_filter;
      sidebar->pending_search_setup = false;
      sidebar->pending_search_query.clear();
      sidebar->pending_search_path_filter.clear();
      run_search(state.get(), workspace, model, indexer, layout_state);
    }
    if (event == Event::Custom && sidebar != nullptr && sidebar->pending_focus_search) {
      sidebar->pending_focus_search = false;
      activate_field(TextInputFocus::SearchQuery, query_input);
      return true;
    }

    if (!search_tab_active(layout_state)) {
      return false;
    }

    if (event_is_tuide_global_shortcut(event)) {
      return false;
    }

    if (event == Event::Escape) {
      if (state->runner.running()) {
        state->runner.cancel();
        state->status = i18n::tr("search.status.cancelling");
        return true;
      }
      clear_search_input_focus(layout_state);
      return true;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      focus->region = FocusRegion::Terminal;
      if (state->query_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchQuery, query_input);
        return true;
      }
      if (state->replace_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchReplace, replace_input);
        return true;
      }
      if (state->path_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchPath, path_input);
        return true;
      }
      if (state->include_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchInclude, include_input);
        return true;
      }
      if (state->exclude_box.Contain(m.x, m.y)) {
        activate_field(TextInputFocus::SearchExclude, exclude_input);
        return true;
      }
      if (state->results_box.Contain(m.x, m.y)) {
        clear_search_input_focus(layout_state);
        const int visible = state->last_visible_lines;
        const int visual_row = m.y - state->results_box.y_min;
        const int row = visual_row + state->first_visible;
        if (row >= 0 && row < static_cast<int>(state->results.size())) {
          state->selected = row;
          clamp_search_scroll(state.get(), visible);
          open_result(state.get(), workspace, model, focus, row);
        }
        return true;
      }
      return false;
    }

    if (event.is_mouse() && state->results_box.Contain(event.mouse().x, event.mouse().y)) {
      const auto& m = event.mouse();
      const int total = static_cast<int>(state->results.size());
      const int visible = visible_line_count(state->results_box);
      state->last_visible_lines = visible;
      const int max_scroll = std::max(0, total - visible);
      if (m.button == Mouse::WheelUp) {
        state->first_visible = std::max(0, state->first_visible - 3);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
        return true;
      }
      if (m.button == Mouse::WheelDown) {
        state->first_visible = std::min(state->first_visible + 3, max_scroll);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
        return true;
      }
    }

    if (event.is_mouse() && event.mouse().motion == Mouse::Moved &&
        state->results_box.Contain(event.mouse().x, event.mouse().y)) {
      focus->region = FocusRegion::Terminal;
      clear_search_input_focus(layout_state);
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
            activate_field(TextInputFocus::SearchInclude, include_input);
            break;
          case TextInputFocus::SearchInclude:
            activate_field(TextInputFocus::SearchExclude, exclude_input);
            break;
          case TextInputFocus::SearchExclude:
            activate_field(TextInputFocus::SearchQuery, query_input);
            break;
          default:
            activate_field(TextInputFocus::SearchQuery, query_input);
            break;
        }
        return true;
      }
      if (event == Event::Return) {
        run_search(state.get(), workspace, model, indexer, layout_state);
        clear_search_input_focus(layout_state);
        return true;
      }
      if (event.is_character() || event == Event::Backspace || event == Event::Delete ||
          event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::Home ||
          event == Event::End) {
        const bool handled = forward_input_event(event);
        if (handled) {
          cursor_blink::show();
          if (state->query != state->committed_query && state->runner.running()) {
            state->runner.cancel();
            state->status = i18n::tr("search.status.enter_to_search");
          }
        }
        return handled;
      }
      return false;
    }

    if (event == Event::Return) {
      if (!state->results.empty()) {
        open_result(state.get(), workspace, model, focus, state->selected);
        return true;
      }
      run_search(state.get(), workspace, model, indexer, layout_state);
      activate_field(TextInputFocus::SearchQuery, query_input);
      return true;
    }
    if (event == Event::Character('r')) {
      if (!state->replace.empty()) {
        run_replace_all(state.get(), workspace, model, indexer, layout_state);
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
        clamp_search_scroll(state.get(), state->last_visible_lines);
        return true;
      }
      return false;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      clamp_search_scroll(state.get(), state->last_visible_lines);
      return true;
    }
    if (event == Event::PageDown) {
      if (!state->results.empty()) {
        const int visible = state->last_visible_lines;
        state->selected =
            std::min(state->selected + visible, static_cast<int>(state->results.size()) - 1);
        clamp_search_scroll(state.get(), visible);
        return true;
      }
      return false;
    }
    if (event == Event::PageUp) {
      if (!state->results.empty()) {
        const int visible = state->last_visible_lines;
        state->selected = std::max(0, state->selected - visible);
        clamp_search_scroll(state.get(), visible);
        return true;
      }
      return false;
    }

    return false;
  };

  if (layout_state != nullptr) {
    layout_state->search_key_handler = handler;
  }

  return WrapFocusable(CatchEvent(
      Renderer(input_layers, [state, layout_state, query_input, replace_input, path_input,
                              include_input, exclude_input, focus] {
        const auto active = layout_state != nullptr ? layout_state->text_input_focus
                                                    : TextInputFocus::None;

        const std::string status_suffix =
            state->runner.running()
                ? i18n::tr("search.status.suffix.searching")
                : (state->query != state->committed_query
                       ? i18n::tr("search.status.suffix.pending")
                       : (state->replace.empty() ? i18n::tr("search.status.enter_to_search")
                                                 : i18n::tr("search.status.suffix.replace")));

        Element form = vbox({
            hbox({
                render_search_field(i18n::tr("search.field.search"), state->query, i18n::tr("search.placeholder.search"), active == TextInputFocus::SearchQuery,
                                    query_input, &state->query_box),
                render_search_field(i18n::tr("search.field.replace"), state->replace, i18n::tr("search.placeholder.replace"),
                                    active == TextInputFocus::SearchReplace, replace_input,
                                    &state->replace_box),
                render_search_field(i18n::tr("search.field.path"), state->path_filter, i18n::tr("search.placeholder.path"),
                                    active == TextInputFocus::SearchPath, path_input,
                                    &state->path_box),
                render_search_field(i18n::tr("search.field.include"), state->include_pattern, i18n::tr("search.placeholder.include"),
                                    active == TextInputFocus::SearchInclude, include_input,
                                    &state->include_box),
                render_search_field(i18n::tr("search.field.exclude"), state->exclude_pattern, i18n::tr("search.placeholder.exclude"),
                                    active == TextInputFocus::SearchExclude, exclude_input,
                                    &state->exclude_box),
            }),
            separator(),
            text(" " + state->status + status_suffix) | color(theme::Muted()) |
                size(HEIGHT, EQUAL, 1),
        });

        Elements rows;
        if (state->results.empty() && state->query.empty()) {
          rows.push_back(text(i18n::tr("common.no_results")) | color(theme::Muted()));
        } else if (state->results.empty()) {
          rows.push_back(text(state->runner.running() ? i18n::tr("search.loading") : i18n::tr("common.no_matches")) |
                         color(theme::Muted()));
        } else {
          const int visible = visible_line_count(state->results_box);
          state->last_visible_lines = visible;
          clamp_search_scroll(state.get(), visible);
          const int total = static_cast<int>(state->results.size());
          const int end = std::min(total, state->first_visible + visible);
          for (int i = state->first_visible; i < end; ++i) {
            const auto& hit = state->results[static_cast<std::size_t>(i)];
            const std::string location = hit.file + ":" + std::to_string(hit.line) + " ";
            const bool selected =
                i == state->selected && focus->region == FocusRegion::Terminal;

            Elements parts;
            parts.push_back(text(" " + location) | color(theme::Accent()));
            std::size_t pos = 0;
            const std::string& highlight = state->committed_query;
            while (pos <= hit.preview.size()) {
              const auto found = highlight.empty() ? std::string::npos : hit.preview.find(highlight, pos);
              if (found == std::string::npos) {
                parts.push_back(text(hit.preview.substr(pos)) | color(theme::Header()));
                break;
              }
              if (found > pos) {
                parts.push_back(text(hit.preview.substr(pos, found - pos)) | color(theme::Header()));
              }
              parts.push_back(text(highlight) | color(theme::Stop()) | bold);
              pos = found + highlight.size();
            }
            Element row = hbox(std::move(parts));
            if (selected) {
              row = row | inverted | bold;
            }
            rows.push_back(std::move(row));
          }
        }

        auto results = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                       reflect(state->results_box) | bgcolor(theme::PanelBg());

        return PanelBody(vbox({std::move(form), std::move(results)}));
      }),
      handler));
}

}  // namespace tuide
