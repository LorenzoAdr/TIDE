#include "ui/external_file_wizard.hpp"
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

bool update_f1_browser_hover(ExternalFileWizardState* state, MainLayoutState* layout_state, int x,
                             int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  const std::string_view before = layout_state->clickable.hovered_id();
  const auto local = local_row_in_box(state->browser.browser_list_box, x, y);
  if (local.has_value()) {
    const int row = state->browser.browser_list_start + *local;
    if (row >= 0 && row < static_cast<int>(state->browser.entries.size())) {
      layout_state->clickable.set_hover(press_id::f1_browser_row(row));
    } else {
      layout_state->clickable.clear_hover_if(press_id::is_f1_hover);
    }
  } else {
    layout_state->clickable.clear_hover_if(press_id::is_f1_hover);
  }
  if (layout_state->clickable.hovered_id() != before) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return false;
}

void activate_external_file_row(ExternalFileWizardState* state, MainLayoutState* layout_state,
                                ExternalFileCompleteCallback on_open, int row) {
  if (row < 0 || row >= static_cast<int>(state->browser.entries.size())) {
    return;
  }
  trigger_press(layout_state, press_id::f1_browser_row(row));
  state->browser.selected = row;
  const auto& entry = state->browser.entries[static_cast<std::size_t>(row)];
  if (entry.is_directory) {
    state->browser.browser_path = entry.path;
    state->browser.reload_browser_entries(true);
    return;
  }
  state->open = false;
  if (on_open) {
    on_open(entry.path);
  }
}

}  // namespace

void ExternalFileWizardState::reset() {
  browser.reset(launch_root);
}

Component MakeExternalFileWizardOverlay(Component main, ExternalFileWizardState* state,
                                        MainLayoutState* layout_state,
                                        ExternalFileCompleteCallback on_open) {
  return Renderer(
      CatchEvent(main, [state, layout_state, on_open](Event event) {
        if (!state->open) {
          return false;
        }

        state->browser.ensure_browser_entries();

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_f1_browser_hover(state, layout_state, event.mouse().x, event.mouse().y);
          return true;
        }

        if (event == Event::Escape) {
          if (state->browser.handle_filter_input(event) == PathBrowserFilterResult::kClearFilter) {
            return true;
          }
          state->open = false;
          return true;
        }

        if (state->browser.handle_filter_input(event) == PathBrowserFilterResult::kHandled) {
          return true;
        }

        if (state->browser.handle_list_navigation(event)) {
          return true;
        }
        if (event == Event::Return || event == Event::Character('o') ||
            event == Event::Character('O')) {
          activate_external_file_row(state, layout_state, on_open, state->browser.selected);
          return true;
        }
        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->browser.browser_list_box.Contain(m.x, m.y)) {
            const int row = state->browser.browser_list_start +
                            (m.y - state->browser.browser_list_box.y_min);
            activate_external_file_row(state, layout_state, on_open, row);
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
        std::string filter_line = state->browser.filter_query;
        filter_line.push_back('_');
        body.push_back(ModalInputLine(filter_line));
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
                                                : i18n::tr("common.browser.file_prefix");
          const std::string row_id = press_id::f1_browser_row(i);
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
            text(i18n::tr("wizard.external_file.title")) | color(theme::Accent()),
            vbox({
                vbox(std::move(body)) | flex | bgcolor(theme::PanelBg()),
                separator(),
                text(i18n::tr("wizard.external_file.footer")) | color(theme::Muted()),
            }))
            | size(WIDTH, GREATER_THAN, 60)
            | size(HEIGHT, GREATER_THAN, 12)
            | bgcolor(theme::PanelBg());

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
