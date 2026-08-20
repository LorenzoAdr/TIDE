#include "ui/open_file_confirm.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/panel.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"
#include "util/file_open_policy.hpp"

namespace fs = std::filesystem;

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kVirtualize = 0;
constexpr int kLoadFull = 1;
constexpr int kCancel = 2;
constexpr int kOk = 0;

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

bool update_hover(OpenFileConfirmState* state, MainLayoutState* layout_state, int x, int y) {
  if (state == nullptr || layout_state == nullptr || !state->is_open()) {
    return false;
  }
  if (state->mode == OpenFileConfirmMode::BinaryWarning ||
      state->mode == OpenFileConfirmMode::TooLarge) {
    return update_panel_hover(layout_state, x, y,
                              {{press_id::kOpenFileYes, &state->yes_box}},
                              press_id::is_open_file_hover);
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kOpenFileYes, &state->yes_box},
       {press_id::kOpenFileLoadFull, &state->load_full_box},
       {press_id::kOpenFileNo, &state->no_box}},
      press_id::is_open_file_hover);
}

void complete_open(WorkspaceModel* workspace,
                   const std::function<void(const std::string& path, int line, int col)>& on_opened,
                   const OpenFileConfirmState* state,
                   WorkspaceModel::LargeFileOpenChoice choice) {
  if (workspace == nullptr || state == nullptr) {
    return;
  }
  if (state->has_position) {
    workspace->open_file_at_confirmed(state->path, state->line, state->col, choice);
    if (on_opened) {
      on_opened(state->path, state->line, state->col);
    }
    return;
  }
  workspace->open_file_confirmed(state->path, choice);
  if (on_opened) {
    on_opened(state->path, 0, 0);
  }
}

}  // namespace

void OpenFileConfirmState::show_binary_warning(const std::string& absolute_path,
                                               const std::string& name) {
  mode = OpenFileConfirmMode::BinaryWarning;
  selected = kOk;
  path = absolute_path;
  display_name = name;
  size_bytes = 0;
  line = 0;
  col = 0;
  has_position = false;
}

void OpenFileConfirmState::show_too_large_warning(const std::string& absolute_path,
                                                  const std::string& name,
                                                  std::uintmax_t size) {
  mode = OpenFileConfirmMode::TooLarge;
  selected = kOk;
  path = absolute_path;
  display_name = name;
  size_bytes = size;
  line = 0;
  col = 0;
  has_position = false;
}

void OpenFileConfirmState::request_large_confirm(const std::string& absolute_path,
                                                 std::uintmax_t size) {
  mode = OpenFileConfirmMode::LargeFile;
  selected = kVirtualize;
  path = absolute_path;
  display_name = fs::path(absolute_path).filename().string();
  size_bytes = size;
  line = 0;
  col = 0;
  has_position = false;
}

void OpenFileConfirmState::close() {
  mode = OpenFileConfirmMode::Closed;
  selected = kOk;
  path.clear();
  display_name.clear();
  size_bytes = 0;
  line = 0;
  col = 0;
  has_position = false;
}

Component MakeOpenFileConfirmOverlay(
    Component main, OpenFileConfirmState* state, MainLayoutState* layout_state,
    WorkspaceModel* workspace,
    std::function<void(const std::string& path, int line, int col)> on_opened) {
  return Renderer(
      CatchEvent(main, [state, layout_state, workspace, on_opened](Event event) {
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
          if (state->mode == OpenFileConfirmMode::BinaryWarning ||
              state->mode == OpenFileConfirmMode::TooLarge) {
            if (state->yes_box.Contain(m.x, m.y)) {
              trigger_press(layout_state, press_id::kOpenFileYes);
              state->close();
              return true;
            }
            return true;
          }
          if (state->yes_box.Contain(m.x, m.y)) {
            state->selected = kVirtualize;
            trigger_press(layout_state, press_id::kOpenFileYes);
            complete_open(workspace, on_opened, state,
                          WorkspaceModel::LargeFileOpenChoice::Virtualize);
            state->close();
            return true;
          }
          if (state->load_full_box.Contain(m.x, m.y)) {
            state->selected = kLoadFull;
            trigger_press(layout_state, press_id::kOpenFileLoadFull);
            complete_open(workspace, on_opened, state,
                          WorkspaceModel::LargeFileOpenChoice::LoadFull);
            state->close();
            return true;
          }
          if (state->no_box.Contain(m.x, m.y)) {
            state->selected = kCancel;
            trigger_press(layout_state, press_id::kOpenFileNo);
            state->close();
            return true;
          }
        }

        if (event == Event::Escape) {
          state->close();
          return true;
        }

        if (state->mode == OpenFileConfirmMode::BinaryWarning ||
            state->mode == OpenFileConfirmMode::TooLarge) {
          if (event == Event::Return || event == Event::Escape) {
            trigger_press(layout_state, press_id::kOpenFileYes);
            state->close();
            return true;
          }
          return true;
        }

        if (event == Event::Return) {
          if (state->selected == kVirtualize) {
            trigger_press(layout_state, press_id::kOpenFileYes);
            complete_open(workspace, on_opened, state,
                          WorkspaceModel::LargeFileOpenChoice::Virtualize);
            state->close();
          } else if (state->selected == kLoadFull) {
            trigger_press(layout_state, press_id::kOpenFileLoadFull);
            complete_open(workspace, on_opened, state,
                          WorkspaceModel::LargeFileOpenChoice::LoadFull);
            state->close();
          } else {
            trigger_press(layout_state, press_id::kOpenFileNo);
            state->close();
          }
          return true;
        }
        if (event == Event::ArrowUp || event == Event::Character('k')) {
          state->selected = std::max(kVirtualize, state->selected - 1);
          return true;
        }
        if (event == Event::ArrowDown || event == Event::Character('j')) {
          state->selected = std::min(kCancel, state->selected + 1);
          return true;
        }
        if (event == Event::ArrowLeft || event == Event::Character('h')) {
          state->selected = std::max(kVirtualize, state->selected - 1);
          return true;
        }
        if (event == Event::ArrowRight || event == Event::Character('l')) {
          state->selected = std::min(kCancel, state->selected + 1);
          return true;
        }
        if (event == Event::Character('1') || event == Event::Character('v') ||
            event == Event::Character('V')) {
          state->selected = kVirtualize;
          return true;
        }
        if (event == Event::Character('2') || event == Event::Character('f') ||
            event == Event::Character('F')) {
          state->selected = kLoadFull;
          return true;
        }
        if (event == Event::Character('3') || event == Event::Character('n') ||
            event == Event::Character('N') || event == Event::Character('c') ||
            event == Event::Character('C')) {
          state->selected = kCancel;
          return true;
        }
        return true;
      }),
      [main, state, layout_state] {
        Element base = main->Render();
        if (state == nullptr || !state->is_open()) {
          return base;
        }

        const bool yes_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kOpenFileYes);
        const bool load_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kOpenFileLoadFull);
        const bool no_hovered =
            layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kOpenFileNo);
        const bool yes_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kOpenFileYes);
        const bool load_pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(press_id::kOpenFileLoadFull);
        const bool no_pressed =
            layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kOpenFileNo);

        Element dialog;
        if (state->mode == OpenFileConfirmMode::BinaryWarning) {
          dialog = ModalWindow(
              text(i18n::tr("modal.open_file.binary.title")) | color(theme::Accent()),
              vbox({
                  text(i18n::tr("modal.open_file.binary.message")) | color(theme::Header()),
                  text(i18n::tr_fmt("common.highlight.wrap", {state->display_name})) |
                      color(theme::Muted()),
                  separator(),
                  hbox({render_choice(i18n::tr("common.ok"), state->selected == kOk, yes_hovered,
                                      yes_pressed, &state->yes_box)}),
                  text(i18n::tr("modal.open_file.binary.footer")) | color(theme::Muted()),
              }));
        } else if (state->mode == OpenFileConfirmMode::TooLarge) {
          const std::string size_label = format_file_size(state->size_bytes);
          dialog = ModalWindow(
              text(i18n::tr("modal.open_file.too_large.title")) | color(theme::Accent()),
              vbox({
                  text(i18n::tr_fmt("modal.open_file.too_large.message",
                                    {state->display_name, size_label})) |
                      color(theme::Header()),
                  separator(),
                  hbox({render_choice(i18n::tr("common.ok"), state->selected == kOk, yes_hovered,
                                      yes_pressed, &state->yes_box)}),
                  text(i18n::tr("modal.open_file.binary.footer")) | color(theme::Muted()),
              }));
        } else {
          const std::string size_label = format_file_size(state->size_bytes);
          dialog = ModalWindow(
              text(i18n::tr("modal.open_file.large.title")) | color(theme::Accent()),
              vbox({
                  text(i18n::tr_fmt("modal.open_file.large.prompt",
                                    {state->display_name, size_label})) |
                      color(theme::Header()),
                  separator(),
                  vbox({
                      render_choice(i18n::tr("modal.open_file.large.virtualize"),
                                    state->selected == kVirtualize, yes_hovered, yes_pressed,
                                    &state->yes_box),
                      render_choice(i18n::tr("modal.open_file.large.load_full"),
                                    state->selected == kLoadFull, load_hovered, load_pressed,
                                    &state->load_full_box),
                      render_choice(i18n::tr("modal.open_file.large.cancel"),
                                    state->selected == kCancel, no_hovered, no_pressed,
                                    &state->no_box),
                  }),
                  text(i18n::tr("modal.open_file.large.footer")) | color(theme::Muted()),
              }));
        }

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tuide
