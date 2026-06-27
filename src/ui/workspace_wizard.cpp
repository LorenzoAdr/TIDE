#include "ui/workspace_wizard.hpp"

#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

void WorkspaceWizardState::reset() {
  browser.reset(launch_root);
}

Component MakeWorkspaceWizardOverlay(Component main, WorkspaceWizardState* state,
                                   WorkspaceCompleteCallback on_complete,
                                   std::function<void()> on_request_quit) {
  return Renderer(
      CatchEvent(main, [state, on_complete, on_request_quit](Event event) {
        if (!state->open) {
          return false;
        }

        state->browser.ensure_browser_entries();

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
          if (state->browser.entries.empty()) {
            return true;
          }
          const auto& row = state->browser.entries[state->browser.selected];
          if (row.is_directory) {
            state->browser.browser_path = row.path;
            state->browser.reload_browser_entries(true);
          }
          return true;
        }
        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->browser.browser_list_box.Contain(m.x, m.y)) {
            const int row = state->browser.browser_list_start +
                            (m.y - state->browser.browser_list_box.y_min);
            if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
              state->browser.selected = row;
              const auto& entry = state->browser.entries[row];
              if (entry.is_directory) {
                state->browser.browser_path = entry.path;
                state->browser.reload_browser_entries(true);
              }
            }
            return true;
          }
        }
        return true;
      }),
      [main, state] {
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
                        std::max(0, static_cast<int>(state->browser.entries.size()) -
                                          max_rows)));
        const int start = state->browser.browser_list_start;
        const int end = std::min(static_cast<int>(state->browser.entries.size()),
                                 start + max_rows);
        Elements list_rows;
        for (int i = start; i < end; ++i) {
          const auto& row = state->browser.entries[i];
          std::string prefix = row.is_directory ? "[dir] " : "      ";
          Element line = text(prefix + row.name);
          if (row.is_directory) {
            line = line | color(theme::Accent());
          }
          if (i == state->browser.selected) {
            line = line | inverted | bold;
          }
          list_rows.push_back(line);
        }
        if (list_rows.empty()) {
          list_rows.push_back(text("(vacío)") | color(theme::Muted()));
        }
        body.push_back(vbox(std::move(list_rows)) |
                       reflect(state->browser.browser_list_box));

        Element dialog = window(
            text("Elegir directorio de trabajo") | color(theme::Accent()),
            vbox({
                vbox(std::move(body)) | flex | bgcolor(theme::PanelBg()),
                separator(),
                text("j/k  Enter carpeta  a=usar carpeta  Esc/q salir") |
                    color(theme::Muted()),
            }))
            | size(WIDTH, GREATER_THAN, 60)
            | size(HEIGHT, GREATER_THAN, 12)
            | bgcolor(theme::PanelBg());

        return dbox({base | bgcolor(theme::PanelBg()) | dim,
                     std::move(dialog) | center});
      });
}

}  // namespace tgdb
