#include "ui/ai_path_scope_modal.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

using namespace ftxui;

void clamp_selection(AiPathScopeModalState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->draft_paths.empty()) {
    state->selected = 0;
    return;
  }
  state->selected =
      std::max(0, std::min(state->selected, static_cast<int>(state->draft_paths.size()) - 1));
}

bool path_already_listed(const AiPathScopeModalState* state, const std::string& path) {
  if (state == nullptr) {
    return false;
  }
  return std::find(state->draft_paths.begin(), state->draft_paths.end(), path) !=
         state->draft_paths.end();
}

std::string to_workspace_relative(const std::string& workspace_root, const std::string& abs_path) {
  if (workspace_root.empty() || abs_path.empty()) {
    return normalize_path(abs_path);
  }
  std::error_code ec;
  const auto rel =
      fs::relative(fs::path(abs_path).lexically_normal(), fs::path(workspace_root).lexically_normal(),
                   ec);
  if (!ec && !rel.empty() && rel.native().rfind("..", 0) != 0) {
    std::string out = rel.generic_string();
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) {
      out.pop_back();
    }
    return out;
  }
  return normalize_path(abs_path);
}

void open_browser(AiPathScopeModalState* state) {
  if (state == nullptr) {
    return;
  }
  const std::string start =
      state->workspace_root.empty() ? state->path_browser.launch_root : state->workspace_root;
  state->path_browser.reset(start);
  state->panel = AiPathScopePanel::kBrowser;
}

void confirm_browser_selection(AiPathScopeModalState* state) {
  if (state == nullptr || !is_directory_path(state->path_browser.browser_path)) {
    return;
  }
  const std::string chosen =
      to_workspace_relative(state->workspace_root, state->path_browser.browser_path);
  if (!chosen.empty() && !path_already_listed(state, chosen)) {
    state->draft_paths.push_back(chosen);
  }
  state->panel = AiPathScopePanel::kList;
  clamp_selection(state);
}

void delete_selected(AiPathScopeModalState* state) {
  if (state == nullptr || state->draft_paths.empty()) {
    return;
  }
  clamp_selection(state);
  const auto index = static_cast<std::size_t>(state->selected);
  if (index >= state->draft_paths.size()) {
    return;
  }
  state->draft_paths.erase(state->draft_paths.begin() + static_cast<std::ptrdiff_t>(index));
  clamp_selection(state);
}

void activate_browser_row(AiPathScopeModalState* state, int row) {
  if (state == nullptr || row < 0 || row >= static_cast<int>(state->path_browser.entries.size())) {
    return;
  }
  state->path_browser.selected = row;
  const auto& entry = state->path_browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->path_browser.browser_path = entry.path;
    state->path_browser.reload_browser_entries(true);
  }
}

Element render_list_body(const AiPathScopeModalState& state) {
  Elements rows;
  rows.push_back(text(i18n::tr("console.ai.path_scope.hint")) | color(theme::Muted()));
  rows.push_back(separator());
  if (state.draft_paths.empty()) {
    rows.push_back(text(i18n::tr("console.ai.path_scope.empty")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state.draft_paths.size()); ++i) {
      Element line = text("  " + state.draft_paths[static_cast<std::size_t>(i)]);
      if (i == state.selected) {
        line = line | inverted;
      }
      rows.push_back(std::move(line));
    }
  }
  rows.push_back(separator());
  rows.push_back(text(i18n::tr("console.ai.path_scope.footer")) | color(theme::Muted()));
  return vbox(std::move(rows));
}

Element render_browser_body(AiPathScopeModalState* state) {
  state->path_browser.ensure_browser_entries();
  Elements rows;
  std::string filter_line = state->path_browser.filter_query;
  filter_line.push_back('_');
  rows.push_back(ModalInputLine(filter_line));
  rows.push_back(text(state->path_browser.browser_path) | color(theme::Muted()));
  rows.push_back(separator());
  if (state->path_browser.entries.empty()) {
    rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
  } else {
    for (int i = 0; i < static_cast<int>(state->path_browser.entries.size()); ++i) {
      const auto& row = state->path_browser.entries[static_cast<std::size_t>(i)];
      const std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                  : i18n::tr("common.browser.file_indent");
      Element line = text(prefix + row.name);
      if (row.is_directory) {
        line = line | color(theme::Accent());
      } else {
        line = line | color(theme::Muted());
      }
      if (i == state->path_browser.selected) {
        line = line | inverted;
      }
      rows.push_back(std::move(line));
    }
  }
  rows.push_back(separator());
  rows.push_back(text(i18n::tr("console.ai.path_scope.browser_footer")) | color(theme::Muted()));
  return vbox(std::move(rows));
}

bool apply_and_close(AiPathScopeModalState* state, const AiPathScopeApplyCallback& on_apply) {
  if (state == nullptr || on_apply == nullptr) {
    return false;
  }
  on_apply(state->draft_paths);
  state->open = false;
  state->panel = AiPathScopePanel::kList;
  return true;
}

}  // namespace

void open_ai_path_scope_modal(AiPathScopeModalState* state, const std::string& workspace_root,
                              const std::vector<std::string>& current_paths) {
  if (state == nullptr) {
    return;
  }
  state->open = true;
  state->panel = AiPathScopePanel::kList;
  state->workspace_root = workspace_root;
  state->draft_paths = current_paths;
  state->selected = 0;
  state->path_browser.launch_root =
      workspace_root.empty() ? canonical_browser_root("") : workspace_root;
  clamp_selection(state);
}

Component MakeAiPathScopeModalOverlay(Component main, AiPathScopeModalState* state,
                                      MainLayoutState* layout_state,
                                      AiPathScopeApplyCallback on_apply) {
  return CatchEvent(
      Renderer(main,
               [main, state, layout_state] {
                 Element base = main->Render();
                 if (state == nullptr || !state->open) {
                   return base;
                 }
                 Element body = state->panel == AiPathScopePanel::kBrowser
                                    ? render_browser_body(state)
                                    : render_list_body(*state);
                 const std::string title_key = state->panel == AiPathScopePanel::kBrowser
                                                   ? "console.ai.path_scope.browser_title"
                                                   : "console.ai.path_scope.title";
                 Element dialog =
                     ModalWindow(text(i18n::tr(title_key)) | color(theme::Accent()), std::move(body));
                 if (layout_state != nullptr) {
                   UI_WAKE(layout_state, "wake");
                 }
                 return ScreenModalOverlay(std::move(base), std::move(dialog));
               }),
      [state, on_apply](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }

        if (state->panel == AiPathScopePanel::kBrowser) {
          state->path_browser.ensure_browser_entries();
          if (event == Event::Escape) {
            if (state->path_browser.handle_filter_input(event) ==
                PathBrowserFilterResult::kClearFilter) {
              return true;
            }
            state->panel = AiPathScopePanel::kList;
            return true;
          }
          if (event == Event::Character('a') || event == Event::Character('A')) {
            confirm_browser_selection(state);
            return true;
          }
          if (state->path_browser.handle_filter_input(event) == PathBrowserFilterResult::kHandled) {
            return true;
          }
          if (state->path_browser.handle_list_navigation(event)) {
            return true;
          }
          if (event == Event::Return) {
            activate_browser_row(state, state->path_browser.selected);
            return true;
          }
          return true;
        }

        // List panel
        if (event == Event::Escape) {
          state->open = false;
          state->panel = AiPathScopePanel::kList;
          return true;
        }
        if (event == Event::Return) {
          return apply_and_close(state, on_apply);
        }
        if (event == Event::Character('a') || event == Event::Character('A')) {
          open_browser(state);
          return true;
        }
        if (event == Event::Character('d') || event == Event::Character('D') ||
            event == Event::Delete || event == Event::Backspace) {
          delete_selected(state);
          return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
          if (!state->draft_paths.empty()) {
            state->selected = std::min(state->selected + 1,
                                       static_cast<int>(state->draft_paths.size()) - 1);
          }
          return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
          state->selected = std::max(0, state->selected - 1);
          return true;
        }
        return true;
      });
}

}  // namespace tuide
