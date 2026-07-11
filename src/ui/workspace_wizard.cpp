#include "ui/workspace_wizard.hpp"
#include "ui/ui_wake.hpp"

#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

bool update_f3_browser_hover(WorkspaceWizardState* state, MainLayoutState* layout_state, int x,
                             int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto local = local_row_in_box(state->browser.browser_list_box, x, y);
  if (local.has_value()) {
    const int row = state->browser.browser_list_start + *local;
    if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
      layout_state->clickable.set_hover(press_id::f3_browser_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_f3_hover);
    }
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f3_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

void activate_workspace_browser_row(WorkspaceWizardState* state, MainLayoutState* layout_state,
                                    int row) {
  if (row < 0 || row >= static_cast<int>(state->browser.entries.size())) {
    return;
  }
  trigger_press(layout_state, press_id::f3_browser_row(row));
  state->browser.selected = row;
  const auto& entry = state->browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->browser.browser_path = entry.path;
    state->browser.reload_browser_entries(true);
  }
}

}  // namespace

void WorkspaceWizardState::reset() {
  browser.reset(launch_root);
}

Component MakeWorkspaceWizardOverlay(Component main, WorkspaceWizardState* state,
                                   MainLayoutState* layout_state,
                                   WorkspaceCompleteCallback on_complete,
                                   std::function<void()> on_request_quit) {
  return Renderer(
      CatchEvent(main, [state, layout_state, on_complete, on_request_quit](Event event) {
        if (!state->open) {
          return false;
        }

        state->browser.ensure_browser_entries();

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_f3_browser_hover(state, layout_state, event.mouse().x, event.mouse().y);
          return true;
        }

        if (event == Event::Escape || event == Event::Character('q')) {
          if (on_request_quit) {
            on_request_quit();
          }
          return true;
        }

        if (event == Event::Character('a') || event == Event::Character('A')) {
          if (!is_directory_path(state->browser.browser_path)) {
            return true;
          }
          state->open = false;
          if (on_complete) {
            on_complete(state->browser.browser_path);
          }
          return true;
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
        if (event == Event::PageDown) {
          state->browser.selected = std::min(
              state->browser.selected + 12,
              std::max(0, static_cast<int>(state->browser.entries.size()) - 1));
          return true;
        }
        if (event == Event::PageUp) {
          state->browser.selected = std::max(0, state->browser.selected - 12);
          return true;
        }
        if (event == Event::Return) {
          activate_workspace_browser_row(state, layout_state, state->browser.selected);
          return true;
        }
        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->browser.browser_list_box.Contain(m.x, m.y)) {
            const int row = state->browser.browser_list_start +
                            (m.y - state->browser.browser_list_box.y_min);
            activate_workspace_browser_row(state, layout_state, row);
            return true;
          }
        }
        return true;
      }),
      [main, state, layout_state] {
        Element base = main->Render();
        if (!state->open) {
          return base;
        }

        state->browser.ensure_browser_entries();

        Elements body;
        body.push_back(text(state->browser.browser_path) | color(theme::Muted()));
        body.push_back(separator());

        const int max_rows = 14;
        state->browser.browser_list_start = std::max(
            0, std::min(state->browser.selected,
                        std::max(0, static_cast<int>(state->browser.entries.size()) - max_rows)));
        const int start = state->browser.browser_list_start;
        const int end = std::min(static_cast<int>(state->browser.entries.size()), start + max_rows);
        Elements list_rows;
        for (int i = start; i < end; ++i) {
          const auto& row = state->browser.entries[static_cast<std::size_t>(i)];
          std::string prefix = row.is_directory ? i18n::tr("common.browser.dir_prefix")
                                                : i18n::tr("common.browser.file_indent");
          const std::string row_id = press_id::f3_browser_row(i);
          const bool selected = i == state->browser.selected;
          const bool hovered =
              layout_state != nullptr && layout_state->clickable.is_hovered(row_id);
          const bool pressed =
              layout_state != nullptr && layout_state->clickable.is_pressed(row_id);
          Element line = text(prefix + row.name);
          if (row.is_directory) {
            line = line | color(theme::Accent());
          }
          line = StyleListRow(std::move(line), selected, hovered, pressed);
          list_rows.push_back(line);
        }
        if (list_rows.empty()) {
          list_rows.push_back(text(i18n::tr("common.empty")) | color(theme::Muted()));
        }
        body.push_back(vbox(std::move(list_rows)) | reflect(state->browser.browser_list_box));

        Element dialog = window(
            text(i18n::tr("wizard.workspace.title")) | color(theme::Accent()),
            vbox({
                vbox(std::move(body)) | flex | bgcolor(theme::PanelBg()),
                separator(),
                text(i18n::tr("wizard.workspace.footer")) | color(theme::Muted()),
            }))
            | size(WIDTH, GREATER_THAN, 60)
            | size(HEIGHT, GREATER_THAN, 12)
            | bgcolor(theme::PanelBg());

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
