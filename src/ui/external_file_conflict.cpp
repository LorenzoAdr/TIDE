#include "ui/external_file_conflict.hpp"

#include <filesystem>
#include <string_view>

#include "app/workspace_model.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace fs = std::filesystem;

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kOverwrite = 0;
constexpr int kLoad = 1;

Element render_choice(std::string_view label, bool selected, bool hovered, bool pressed, Box* box) {
  Element row = text(std::string(label)) | color(theme::Header());
  if (pressed) {
    row = row | inverted | bold | bgcolor(theme::TabPressed());
  } else if (hovered) {
    row = row | bold | bgcolor(theme::TabHover());
  } else if (selected) {
    row = row | inverted | bold;
  }
  return row | reflect(*box);
}

bool update_hover(ExternalFileConflictState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->is_open()) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kExternalConflictOverwrite, &state->overwrite_box},
       {press_id::kExternalConflictLoad, &state->load_box}},
      press_id::is_external_conflict_hover);
}

void acknowledge_and_close(ExternalFileConflictState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->workspace != nullptr) {
    state->workspace->acknowledge_external_disk_mtime(state->path, state->disk_mtime_sec);
  }
  state->close();
}

void choose_overwrite(ExternalFileConflictState* state,
                      const std::function<void(WorkspaceModel*)>& on_resolved) {
  if (state == nullptr || state->workspace == nullptr) {
    return;
  }
  WorkspaceModel* workspace = state->workspace;
  workspace->save_buffer();
  state->close();
  if (on_resolved) {
    on_resolved(workspace);
  }
}

void choose_load(ExternalFileConflictState* state,
                 const std::function<void(WorkspaceModel*)>& on_resolved) {
  if (state == nullptr || state->workspace == nullptr) {
    return;
  }
  WorkspaceModel* workspace = state->workspace;
  workspace->reload_active_tab_from_disk();
  state->close();
  if (on_resolved) {
    on_resolved(workspace);
  }
}

}  // namespace

void ExternalFileConflictState::show(WorkspaceModel* owner, const std::string& absolute_path,
                                     std::int64_t mtime_sec) {
  open = true;
  selected = kLoad;
  path = absolute_path;
  display_name = fs::path(absolute_path).filename().string();
  disk_mtime_sec = mtime_sec;
  workspace = owner;
}

void ExternalFileConflictState::close() {
  open = false;
  selected = kLoad;
  path.clear();
  display_name.clear();
  disk_mtime_sec = 0;
  workspace = nullptr;
}

Component MakeExternalFileConflictOverlay(
    Component main, ExternalFileConflictState* state, MainLayoutState* layout_state,
    std::function<void(WorkspaceModel* workspace)> on_resolved) {
  return Renderer(
      CatchEvent(main, [state, layout_state, on_resolved](Event event) {
        if (state == nullptr || !state->is_open()) {
          return false;
        }

        if (event.is_mouse() && event.mouse().motion == Mouse::Moved) {
          update_hover(state, layout_state, event.mouse().x, event.mouse().y);
          return false;
        }

        if (event.is_mouse() && event.mouse().button == Mouse::Left &&
            event.mouse().motion == Mouse::Pressed) {
          const auto& m = event.mouse();
          if (state->overwrite_box.Contain(m.x, m.y)) {
            state->selected = kOverwrite;
            trigger_press(layout_state, press_id::kExternalConflictOverwrite);
            choose_overwrite(state, on_resolved);
            return true;
          }
          if (state->load_box.Contain(m.x, m.y)) {
            state->selected = kLoad;
            trigger_press(layout_state, press_id::kExternalConflictLoad);
            choose_load(state, on_resolved);
            return true;
          }
        }

        if (event == Event::Escape) {
          acknowledge_and_close(state);
          return true;
        }
        if (event == Event::Return) {
          if (state->selected == kOverwrite) {
            trigger_press(layout_state, press_id::kExternalConflictOverwrite);
            choose_overwrite(state, on_resolved);
          } else {
            trigger_press(layout_state, press_id::kExternalConflictLoad);
            choose_load(state, on_resolved);
          }
          return true;
        }
        if (event == Event::ArrowLeft || event == Event::Character('h')) {
          state->selected = kOverwrite;
          return true;
        }
        if (event == Event::ArrowRight || event == Event::Character('l')) {
          state->selected = kLoad;
          return true;
        }
        if (event == Event::Character('s') || event == Event::Character('S') ||
            event == Event::Character('o') || event == Event::Character('O')) {
          state->selected = kOverwrite;
          return true;
        }
        if (event == Event::Character('c') || event == Event::Character('C') ||
            event == Event::Character('r') || event == Event::Character('R')) {
          state->selected = kLoad;
          return true;
        }
        return true;
      }),
      [main, state, layout_state] {
        Element base = main->Render();
        if (state == nullptr || !state->is_open()) {
          return base;
        }

        const bool overwrite_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kExternalConflictOverwrite);
        const bool load_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kExternalConflictLoad);
        const bool overwrite_pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kExternalConflictOverwrite);
        const bool load_pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kExternalConflictLoad);

        Element dialog = ModalWindow(
            text(i18n::tr("modal.external_conflict.title")) | color(theme::Accent()),
            vbox({
                text(i18n::tr("modal.external_conflict.message")) | color(theme::Header()),
                text(i18n::tr_fmt("common.highlight.wrap", {state->display_name})) |
                    color(theme::Muted()),
                text(i18n::tr("modal.external_conflict.warning")) | color(theme::Header()),
                separator(),
                hbox({
                    render_choice(i18n::tr("modal.external_conflict.overwrite"),
                                  state->selected == kOverwrite, overwrite_hovered,
                                  overwrite_pressed, &state->overwrite_box),
                    text("  "),
                    render_choice(i18n::tr("modal.external_conflict.load"),
                                  state->selected == kLoad, load_hovered, load_pressed,
                                  &state->load_box),
                }),
                text(i18n::tr("modal.external_conflict.footer")) | color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tuide
