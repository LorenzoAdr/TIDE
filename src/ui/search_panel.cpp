#include "ui/search_panel.hpp"
#include "ui/busy_strip.hpp"
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
#include "ui/clickable.hpp"
#include "ui/focusable_component.hpp"
#include "ui/hover_effects.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"
#include "ui/key_bindings.hpp"
#include "i18n/tr.hpp"
#include "editor/text_ops.hpp"

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
  // When >= 0 and different from the query input cursor, the query field has a
  // selection spanning [min(anchor, cursor), max(anchor, cursor)).
  int query_selection_anchor = -1;
  std::vector<WorkspaceSearchResult> results;
  int selected = 0;
  int first_visible = 0;
  int last_visible_lines = 1;
  Box results_box;
  Box list_content_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
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

void clamp_search_scroll_viewport(SearchPanelState* state, int visible_lines) {
  if (state == nullptr) {
    return;
  }
  const int total = static_cast<int>(state->results.size());
  const int max_first = std::max(0, total - visible_lines);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
}

void ensure_search_selection_visible(SearchPanelState* state, int visible_lines) {
  if (state == nullptr) {
    return;
  }
  clamp_search_scroll_viewport(state, visible_lines);
  if (state->selected < state->first_visible) {
    state->first_visible = state->selected;
  } else if (state->selected >= state->first_visible + visible_lines) {
    state->first_visible = state->selected - visible_lines + 1;
  }
  clamp_search_scroll_viewport(state, visible_lines);
}

bool scroll_search_by_wheel(SearchPanelState* state, int delta, int visible_lines) {
  if (state == nullptr) {
    return false;
  }
  const int total = static_cast<int>(state->results.size());
  const int max_first = std::max(0, total - visible_lines);
  const int next = std::max(0, std::min(state->first_visible + delta, max_first));
  if (next == state->first_visible) {
    return false;
  }
  state->first_visible = next;
  return true;
}

bool handle_search_scrollbar_mouse(SearchPanelState* state, MainLayoutState* layout_state,
                                   const Mouse& m, int total, int visible) {
  if (state == nullptr) {
    return false;
  }

  const int max_first = std::max(0, total - visible);
  const bool in_bar = state->scrollbar_box.Contain(m.x, m.y);
  const bool scrollable = state->scrollbar_layout.scrollable;

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || state->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kSearchScrollbar);
      } else {
        layout_state->clickable.clear_hover_if(
            [](std::string_view id) { return id == press_id::kSearchScrollbar; });
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
    if (m.button == Mouse::Left && m.motion == Mouse::Moved && scrollable) {
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
    return scroll_search_by_wheel(state, -3, visible);
  }
  if (m.button == Mouse::WheelDown) {
    return scroll_search_by_wheel(state, 3, visible);
  }

  if (!scrollable) {
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    trigger_press(layout_state, press_id::kSearchScrollbar);
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

}  // namespace

void clear_search_input_focus(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return;
  }
  if (is_search_input_focus(layout_state->text_input_focus)) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
}

bool search_query_has_selection(const SearchPanelState& state, int cursor_pos) {
  return state.query_selection_anchor >= 0 && state.query_selection_anchor != cursor_pos;
}

void clear_search_query_selection(SearchPanelState* state) {
  if (state != nullptr) {
    state->query_selection_anchor = -1;
  }
}

void select_all_search_query(SearchPanelState* state, InputOption* query_option) {
  if (state == nullptr) {
    return;
  }
  if (state->query.empty()) {
    state->query_selection_anchor = -1;
    if (query_option != nullptr) {
      query_option->cursor_position() = 0;
    }
    return;
  }
  state->query_selection_anchor = 0;
  if (query_option != nullptr) {
    query_option->cursor_position() = static_cast<int>(state->query.size());
  }
}

void replace_search_query_selection(SearchPanelState* state, InputOption* query_option,
                                    const std::string& text) {
  if (state == nullptr) {
    return;
  }
  int cursor = query_option != nullptr ? query_option->cursor_position() : 0;
  cursor = std::max(0, std::min(cursor, static_cast<int>(state->query.size())));
  if (search_query_has_selection(*state, cursor)) {
    const int start = std::min(state->query_selection_anchor, cursor);
    const int end = std::max(state->query_selection_anchor, cursor);
    state->query.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    cursor = start;
  }
  state->query_selection_anchor = -1;
  if (!text.empty()) {
    state->query.insert(static_cast<std::size_t>(cursor), text);
    cursor += static_cast<int>(text.size());
  }
  if (query_option != nullptr) {
    query_option->cursor_position() = cursor;
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
  // Share the indexer file list without copying it on the UI thread. Scanning /
  // fallback happens inside WorkspaceSearchRunner's worker.
  if (indexer != nullptr) {
    if (auto snap = indexer->snapshot()) {
      if (snap->workspace_root == opts.workspace_root && !snap->files.empty()) {
        opts.files_ref =
            std::shared_ptr<const std::vector<std::string>>(snap, &snap->files);
      }
    }
  }
  return opts;
}

void apply_search_results(SearchPanelState* state, std::vector<WorkspaceSearchResult> results,
                          bool cancelled, int files_scanned, bool used_rg,
                          MainLayoutState* layout_state = nullptr) {
  if (state == nullptr) {
    return;
  }
  clear_busy(layout_state);
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
  apply_search_results(state, std::move(results), cancelled, files_scanned, used_rg, layout_state);
  if (layout_state != nullptr) {
    wake_console_panel(layout_state);
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
    clear_busy(layout_state);
    return;
  }
  state->committed_query = state->query;
  auto opts = build_options(state, workspace, model, indexer);
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
  state->runner.start(std::move(opts));
  set_busy_spinner(layout_state, BusyActivity::ProjectSearch);
  if (layout_state != nullptr) {
    wake_console_panel(layout_state);
  }
}

void run_replace_all(SearchPanelState* state, WorkspaceModel* workspace, DebugModel* model,
                     WorkspaceIndexer* indexer, MainLayoutState* layout_state) {
  if (state->query.empty() || state->replace.empty()) {
    state->status = i18n::tr("search.status.need_replace");
    return;
  }
  state->runner.cancel();
  auto opts = build_options(state, workspace, model, indexer);
  if (workspace_search_files(opts).empty() && !opts.workspace_root.empty()) {
    opts.files = scan_workspace_files(opts.workspace_root);
  }
  const auto result = replace_in_workspace(opts, state->replace);
  run_search(state, workspace, model, indexer, layout_state);
  state->status = i18n::tr_fmt("search.status.replaced", {std::to_string(result.replacements), std::to_string(result.files_modified)});

  if (workspace != nullptr && !workspace->buffer.path.empty()) {
    namespace fs = std::filesystem;
    for (const auto& rel : workspace_search_files(opts)) {
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
      ensure_scroll_centered(&workspace->buffer, std::max(1, state->last_visible_lines));
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
  if (layout_state != nullptr) {
    state->runner.set_wake_callback([layout_state] { wake_console_panel(layout_state); });
  }

  auto query_option = std::make_shared<InputOption>(MakeBlinkInputOption(
      &state->query, &state->placeholder_query, false, &state->query_selection_anchor));
  auto query_input = Input(*query_option);
  auto replace_input = Input(MakeBlinkInputOption(&state->replace, &state->placeholder_replace));
  auto path_input = Input(MakeBlinkInputOption(&state->path_filter, &state->placeholder_path));
  auto include_input =
      Input(MakeBlinkInputOption(&state->include_pattern, &state->placeholder_include));
  auto exclude_input =
      Input(MakeBlinkInputOption(&state->exclude_pattern, &state->placeholder_exclude));

  auto input_layers = Container::Horizontal(
      {query_input | flex, replace_input | flex, path_input | flex, include_input | flex,
       exclude_input | flex});

  auto activate_field = [layout_state, focus, state, query_option](TextInputFocus field,
                                                                  Component input) {
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
    if (layout_state != nullptr) {
      layout_state->text_input_focus = field;
    }
    if (field == TextInputFocus::SearchQuery) {
      // Restore last query with text preselected (Enter re-runs; typing replaces).
      select_all_search_query(state.get(), query_option.get());
    } else {
      clear_search_query_selection(state.get());
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
                  replace_input, path_input, include_input, exclude_input, query_option,
                  activate_field, forward_input_event](Event event) {
    if (event == Event::Custom) {
      poll_search_results(state.get(), layout_state);
      if (state->runner.running() && layout_state != nullptr) {
        wake_console_panel(layout_state);
      }
    }
    if (event == Event::Custom && sidebar != nullptr && sidebar->pending_search_setup) {
      state->query = sidebar->pending_search_query;
      state->path_filter = sidebar->pending_search_path_filter;
      sidebar->pending_search_setup = false;
      sidebar->pending_search_query.clear();
      sidebar->pending_search_path_filter.clear();
      select_all_search_query(state.get(), query_option.get());
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
        state->status = i18n::tr("search.status.cancelled");
        return true;
      }
      clear_search_query_selection(state.get());
      clear_search_input_focus(layout_state);
      return true;
    }

    if (event.is_mouse()) {
      const auto& m = event.mouse();
      const int total = static_cast<int>(state->results.size());
      const int visible = visible_line_count(state->results_box);
      state->last_visible_lines = visible;

      if (handle_search_scrollbar_mouse(state.get(), layout_state, m, total, visible)) {
        if (layout_state != nullptr) {
          wake_console_panel(layout_state);
        }
        return true;
      }

      if ((state->list_content_box.Contain(m.x, m.y) || state->scrollbar_box.Contain(m.x, m.y)) &&
          (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown)) {
        const int delta = m.button == Mouse::WheelUp ? -3 : 3;
        if (scroll_search_by_wheel(state.get(), delta, visible)) {
          if (layout_state != nullptr) {
            wake_console_panel(layout_state);
          }
        }
        return true;
      }
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      const bool in_search_chrome =
          state->query_box.Contain(m.x, m.y) || state->replace_box.Contain(m.x, m.y) ||
          state->path_box.Contain(m.x, m.y) || state->include_box.Contain(m.x, m.y) ||
          state->exclude_box.Contain(m.x, m.y) || state->list_content_box.Contain(m.x, m.y);
      if (!in_search_chrome) {
        return false;
      }
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
      if (state->list_content_box.Contain(m.x, m.y)) {
        clear_search_query_selection(state.get());
        clear_search_input_focus(layout_state);
        const int visible = state->last_visible_lines;
        const int visual_row = m.y - state->list_content_box.y_min;
        const int row = visual_row + state->first_visible;
        if (row >= 0 && row < static_cast<int>(state->results.size())) {
          state->selected = row;
          ensure_search_selection_visible(state.get(), visible);
          open_result(state.get(), workspace, model, focus, row);
        }
        return true;
      }
      return false;
    }

    if (event.is_mouse() && event.mouse().motion == Mouse::Moved &&
        (state->results_box.Contain(event.mouse().x, event.mouse().y) ||
         state->scrollbar_box.Contain(event.mouse().x, event.mouse().y))) {
      if (focus != nullptr) {
        focus->region = FocusRegion::Terminal;
      }
      clear_search_query_selection(state.get());
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
        clear_search_query_selection(state.get());
        clear_search_input_focus(layout_state);
        return true;
      }
      if (layout_state->text_input_focus == TextInputFocus::SearchQuery) {
        const int cursor = query_option->cursor_position();
        if (search_query_has_selection(*state, cursor)) {
          if (event == Event::Backspace || event == Event::Delete) {
            replace_search_query_selection(state.get(), query_option.get(), "");
            cursor_blink::show();
            if (state->query != state->committed_query && state->runner.running()) {
              state->runner.cancel();
              state->status = i18n::tr("search.status.enter_to_search");
            }
            return true;
          }
          if (event == Event::ArrowLeft) {
            query_option->cursor_position() =
                std::min(state->query_selection_anchor, cursor);
            clear_search_query_selection(state.get());
            cursor_blink::show();
            return true;
          }
          if (event == Event::ArrowRight) {
            query_option->cursor_position() =
                std::max(state->query_selection_anchor, cursor);
            clear_search_query_selection(state.get());
            cursor_blink::show();
            return true;
          }
          if (event == Event::Home) {
            query_option->cursor_position() = 0;
            clear_search_query_selection(state.get());
            cursor_blink::show();
            return true;
          }
          if (event == Event::End) {
            query_option->cursor_position() = static_cast<int>(state->query.size());
            clear_search_query_selection(state.get());
            cursor_blink::show();
            return true;
          }
          if (event.is_character()) {
            const std::string ch = event.character();
            if (!ch.empty() && ch[0] >= 32 && ch[0] != 127) {
              replace_search_query_selection(state.get(), query_option.get(), ch);
              cursor_blink::show();
              if (state->query != state->committed_query && state->runner.running()) {
                state->runner.cancel();
                state->status = i18n::tr("search.status.enter_to_search");
              }
            }
            return true;
          }
        }
      }
      if (event.is_character() || event == Event::Backspace || event == Event::Delete ||
          event == Event::ArrowLeft || event == Event::ArrowRight || event == Event::Home ||
          event == Event::End) {
        const bool handled = forward_input_event(event);
        if (handled) {
          if (layout_state->text_input_focus == TextInputFocus::SearchQuery) {
            clear_search_query_selection(state.get());
          }
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
        ensure_search_selection_visible(state.get(), state->last_visible_lines);
        return true;
      }
      return false;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      ensure_search_selection_visible(state.get(), state->last_visible_lines);
      return true;
    }
    if (event == Event::PageDown) {
      if (!state->results.empty()) {
        const int visible = state->last_visible_lines;
        state->selected =
            std::min(state->selected + visible, static_cast<int>(state->results.size()) - 1);
        ensure_search_selection_visible(state.get(), visible);
        return true;
      }
      return false;
    }
    if (event == Event::PageUp) {
      if (!state->results.empty()) {
        const int visible = state->last_visible_lines;
        state->selected = std::max(0, state->selected - visible);
        ensure_search_selection_visible(state.get(), visible);
        return true;
      }
      return false;
    }

    return false;
  };

  auto wrapped_handler = [handler, layout_state](Event event) {
    const bool handled = handler(event);
    if (handled) {
      // Console is cached: typing/navigation must dirty the panel or the
      // Input Element is reused and the caret/text look stuck until Enter.
      wake_console_panel(layout_state, "search.input");
    }
    return handled;
  };

  if (layout_state != nullptr) {
    layout_state->search_key_handler = wrapped_handler;
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
        const int visible = visible_line_count(state->results_box);
        state->last_visible_lines = visible;
        clamp_search_scroll_viewport(state.get(), visible);
        const int total = static_cast<int>(state->results.size());

        if (state->results.empty() && state->query.empty()) {
          rows.push_back(text(i18n::tr("common.no_results")) | color(theme::Muted()));
        } else if (state->results.empty()) {
          rows.push_back(text(state->runner.running() ? i18n::tr("search.loading") : i18n::tr("common.no_matches")) |
                         color(theme::Muted()));
        } else {
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

        state->scrollbar_layout =
            compute_scrollbar_layout(total, state->first_visible, visible, visible);
        const bool scrollbar_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kSearchScrollbar);
        const bool scrollbar_active =
            state->scrollbar_dragging ||
            (layout_state != nullptr &&
             layout_state->clickable.is_pressed(press_id::kSearchScrollbar));
        Element scrollbar =
            vertical_scrollbar(total, state->first_visible, visible, visible, scrollbar_hovered,
                               scrollbar_active) |
            reflect(state->scrollbar_box);
        Element list_body = vbox(std::move(rows)) | flex | reflect(state->list_content_box);
        auto results = hbox({std::move(list_body) | flex, std::move(scrollbar)}) | flex |
                       reflect(state->results_box) | bgcolor(theme::PanelBg());

        return PanelBody(vbox({std::move(form), std::move(results)}));
      }),
      wrapped_handler));
}

}  // namespace tuide
