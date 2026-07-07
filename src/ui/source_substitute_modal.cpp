#include "ui/source_substitute_modal.hpp"

#include <filesystem>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

using namespace ftxui;

std::string derive_substitute_from_path(const DebugModel& model) {
  auto parent_dir = [](const std::string& file_path) -> std::string {
    if (file_path.empty()) {
      return {};
    }
    const fs::path path(file_path);
    if (path.has_parent_path()) {
      return normalize_path(path.parent_path().string());
    }
    return normalize_path(file_path);
  };

  if (!model.active_file.empty()) {
    return parent_dir(model.active_file);
  }
  if (!model.stack_frames.empty()) {
    const std::size_t index =
        std::min(static_cast<std::size_t>(model.selected_frame), model.stack_frames.size() - 1);
    return parent_dir(model.stack_frames[index].file);
  }
  return {};
}

std::string initial_substitute_to_path(const DebugModel& model, const std::string& workspace_root) {
  if (!model.source_substitute_to.empty()) {
    return model.source_substitute_to;
  }
  if (!workspace_root.empty()) {
    return workspace_root;
  }
  if (!model.active_file.empty() && fs::path(model.active_file).is_absolute()) {
    const fs::path parent = fs::path(model.active_file).parent_path();
    if (!parent.empty()) {
      return normalize_path(parent.string());
    }
  }
  return canonical_browser_root("");
}

void activate_browser_row(SourceSubstituteModalState* state, int row) {
  if (state == nullptr || row < 0 || row >= static_cast<int>(state->browser.entries.size())) {
    return;
  }
  state->browser.selected = row;
  const auto& entry = state->browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->browser.browser_path = entry.path;
    state->browser.reload_browser_entries(true);
  }
}

bool apply_substitute(SourceSubstituteModalState* state, SourceSubstituteApplyCallback on_apply) {
  if (state == nullptr || on_apply == nullptr) {
    return false;
  }
  if (state->from_path.empty() || state->browser.browser_path.empty() ||
      !is_directory_path(state->browser.browser_path)) {
    return false;
  }
  const std::string from = normalize_path(state->from_path);
  const std::string to = normalize_path(state->browser.browser_path);
  on_apply(from, to);
  state->open = false;
  return true;
}

Element render_browser_rows(const SourceSubstituteModalState& state) {
  Elements rows;
  if (state.browser.entries.empty()) {
    rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
    return vbox(std::move(rows));
  }

  for (int i = 0; i < static_cast<int>(state.browser.entries.size()); ++i) {
    const auto& row = state.browser.entries[static_cast<std::size_t>(i)];
    const std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                : i18n::tr("common.browser.file_indent");
    Element line = text(prefix + row.name);
    if (row.is_directory) {
      line = line | color(theme::Accent());
    } else {
      line = line | color(theme::Muted());
    }
    if (i == state.browser.selected) {
      line = line | inverted;
    }
    rows.push_back(std::move(line));
  }
  return vbox(std::move(rows));
}

}  // namespace

void open_source_substitute_modal(SourceSubstituteModalState* state, DebugModel* model,
                                  const std::string& workspace_root) {
  if (state == nullptr || model == nullptr) {
    return;
  }
  state->open = true;
  state->from_path = derive_substitute_from_path(*model);
  state->browser.launch_root =
      workspace_root.empty() ? canonical_browser_root("") : workspace_root;
  state->browser.reset(initial_substitute_to_path(*model, workspace_root));
}

Component MakeSourceSubstituteModalOverlay(Component main, SourceSubstituteModalState* state,
                                           MainLayoutState* layout_state,
                                           SourceSubstituteApplyCallback on_apply) {
  return CatchEvent(
      Renderer(main, [main, state, layout_state, on_apply] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        state->browser.ensure_browser_entries();

        Elements body;
        body.push_back(text(i18n::tr("debug.source_substitute.from_label")) | color(theme::Header()));
        body.push_back(text(state->from_path.empty() ? i18n::tr("common.none")
                                                     : state->from_path) |
                       color(theme::Muted()));
        body.push_back(separator());
        body.push_back(text(i18n::tr("debug.source_substitute.to_label")) | color(theme::Header()));
        body.push_back(text(state->browser.browser_path) | color(theme::Accent()));
        body.push_back(separator());
        body.push_back(render_browser_rows(*state));
        body.push_back(separator());
        body.push_back(text(i18n::tr("debug.source_substitute.footer")) | color(theme::Muted()));

        Element dialog =
            ModalWindow(text(i18n::tr("debug.source_substitute.title")) | color(theme::Accent()),
                        vbox(std::move(body)));

        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
        return ScreenModalOverlay(std::move(base), std::move(dialog));
      }),
      [state, on_apply](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }

        state->browser.ensure_browser_entries();

        if (event == Event::Escape) {
          state->open = false;
          return true;
        }
        if (event == Event::Character('a') || event == Event::Character('A')) {
          return apply_substitute(state, on_apply);
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
          state->browser.selected = std::min(
              state->browser.selected + 1,
              std::max(0, static_cast<int>(state->browser.entries.size()) - 1));
          return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
          state->browser.selected = std::max(0, state->browser.selected - 1);
          return true;
        }
        if (event == Event::Return) {
          activate_browser_row(state, state->browser.selected);
          return true;
        }
        return true;
      });
}

}  // namespace tgdb
