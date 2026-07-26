#include "ui/welcome_screen.hpp"

#include <cstdlib>
#include <vector>

#include "i18n/tr.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kWelcomeRowWidth = 44;

Element centered_row(Element row) {
  return hbox({filler(), std::move(row), filler()});
}

std::string home_prefix() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  std::string prefix = home;
  if (!prefix.empty() && prefix.back() == '/') {
    prefix.pop_back();
  }
  return prefix;
}

std::string display_project_path(const std::string& path) {
  const std::string home = home_prefix();
  std::string display = path;
  if (!home.empty() && display.rfind(home, 0) == 0) {
    display = "~" + display.substr(home.size());
  }
  constexpr int kMaxLen = 38;
  if (static_cast<int>(display.size()) <= kMaxLen) {
    return display;
  }
  return "…" + display.substr(display.size() - static_cast<std::size_t>(kMaxLen - 1));
}

void ensure_recent_boxes(WelcomeScreenState* state) {
  if (state == nullptr) {
    return;
  }
  if (state->recent_project_boxes.size() != state->recent_projects.size()) {
    state->recent_project_boxes.assign(state->recent_projects.size(), Box{});
  }
  if (state->selected_recent >= static_cast<int>(state->recent_projects.size())) {
    state->selected_recent = state->recent_projects.empty()
                                 ? -1
                                 : static_cast<int>(state->recent_projects.size()) - 1;
  }
}

Element render_action_row(const std::string& label, Box* box, MainLayoutState* layout,
                          std::string_view id) {
  const bool hovered = layout != nullptr && layout->clickable.is_hovered(id);
  const bool pressed = layout != nullptr && layout->clickable.is_pressed(id);
  Element row = text(i18n::tr("common.action.prefix") + label) | color(theme::Header());
  if (pressed) {
    row = row | inverted | bold | bgcolor(theme::TabPressed());
  } else if (hovered) {
    row = row | bold | bgcolor(theme::TabHover());
  }
  return row | size(WIDTH, EQUAL, kWelcomeRowWidth) | reflect(*box);
}

Element render_recent_row(WelcomeScreenState* state, MainLayoutState* layout, int index) {
  ensure_recent_boxes(state);
  if (state == nullptr || index < 0 ||
      index >= static_cast<int>(state->recent_projects.size())) {
    return text("");
  }
  const std::string id = press_id::welcome_recent(index);
  const bool hovered = layout != nullptr && layout->clickable.is_hovered(id);
  const bool pressed = layout != nullptr && layout->clickable.is_pressed(id);
  const bool selected = state->selected_recent == index;
  const std::string label =
      i18n::tr("common.action.prefix") + display_project_path(state->recent_projects[static_cast<std::size_t>(index)]);
  Element row = text(label) | color(theme::TitleText());
  row = StyleListRow(std::move(row), selected, hovered, pressed);
  return row | size(WIDTH, EQUAL, kWelcomeRowWidth) |
         reflect(state->recent_project_boxes[static_cast<std::size_t>(index)]);
}

bool update_welcome_hover(WelcomeScreenState* state, MainLayoutState* layout_state, int x,
                          int y) {
  if (state == nullptr || layout_state == nullptr || !chrome_hover_allowed(layout_state)) {
    return false;
  }
  ensure_recent_boxes(state);

  const std::string_view before = layout_state->clickable.hovered_id();
  const int previous_selected = state->selected_recent;

  if (state->external_file_action_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::kWelcomeExternalFile);
  } else if (state->debug_action_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::kWelcomeDebug);
  } else if (state->workspace_action_box.Contain(x, y)) {
    layout_state->clickable.set_hover(press_id::kWelcomeWorkspace);
  } else {
    bool hit_recent = false;
    for (std::size_t i = 0; i < state->recent_project_boxes.size(); ++i) {
      if (state->recent_project_boxes[i].Contain(x, y)) {
        layout_state->clickable.set_hover(press_id::welcome_recent(static_cast<int>(i)));
        state->selected_recent = static_cast<int>(i);
        hit_recent = true;
        break;
      }
    }
    if (!hit_recent) {
      layout_state->clickable.clear_hover_if(press_id::is_welcome_hover);
    }
  }

  if (state->selected_recent != previous_selected) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  return apply_hover_repaint(layout_state, before);
}

bool open_recent_at(WelcomeScreenState* state, MainLayoutState* layout_state, int index,
                    const std::function<void(const std::string&)>& on_recent_project) {
  if (state == nullptr || index < 0 ||
      index >= static_cast<int>(state->recent_projects.size())) {
    return false;
  }
  trigger_press(layout_state, press_id::welcome_recent(index));
  state->selected_recent = index;
  if (on_recent_project) {
    on_recent_project(state->recent_projects[static_cast<std::size_t>(index)]);
  }
  return true;
}

bool handle_welcome_mouse(WelcomeScreenState* state, MainLayoutState* layout_state,
                          const Mouse& m, const std::function<void()>& on_external_file,
                          const std::function<void()>& on_debug,
                          const std::function<void()>& on_workspace,
                          const std::function<void(const std::string&)>& on_recent_project) {
  if (state == nullptr || layout_state == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed) {
    return false;
  }
  if (state->external_file_action_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, press_id::kWelcomeExternalFile);
    if (on_external_file) {
      on_external_file();
    }
    return true;
  }
  if (state->debug_action_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, press_id::kWelcomeDebug);
    if (on_debug) {
      on_debug();
    }
    return true;
  }
  if (state->workspace_action_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, press_id::kWelcomeWorkspace);
    if (on_workspace) {
      on_workspace();
    }
    return true;
  }
  ensure_recent_boxes(state);
  for (std::size_t i = 0; i < state->recent_project_boxes.size(); ++i) {
    if (state->recent_project_boxes[i].Contain(m.x, m.y)) {
      return open_recent_at(state, layout_state, static_cast<int>(i), on_recent_project);
    }
  }
  return false;
}

bool move_recent_selection(WelcomeScreenState* state, int delta) {
  if (state == nullptr || state->recent_projects.empty()) {
    return false;
  }
  const int count = static_cast<int>(state->recent_projects.size());
  if (state->selected_recent < 0) {
    state->selected_recent = delta > 0 ? 0 : count - 1;
  } else {
    state->selected_recent = (state->selected_recent + delta + count) % count;
  }
  return true;
}

Element render_welcome_screen(WelcomeScreenState* state, MainLayoutState* layout_state) {
  ensure_recent_boxes(state);
  Elements body;
  body.push_back(RenderTuideLogo());
  body.push_back(text(""));
  body.push_back(
      centered_row(text(i18n::tr("welcome.tagline")) | color(theme::Muted())));
  body.push_back(text(""));
  body.push_back(render_action_row(i18n::tr("welcome.action.open_external"),
                                   &state->external_file_action_box, layout_state,
                                   press_id::kWelcomeExternalFile));
  body.push_back(text(""));
  body.push_back(render_action_row(i18n::tr("welcome.action.start_debug"), &state->debug_action_box,
                                   layout_state, press_id::kWelcomeDebug));
  body.push_back(text(""));
  body.push_back(render_action_row(i18n::tr("welcome.action.open_workspace"),
                                   &state->workspace_action_box, layout_state,
                                   press_id::kWelcomeWorkspace));

  if (!state->recent_projects.empty()) {
    body.push_back(text(""));
    body.push_back(centered_row(text(i18n::tr("welcome.recent.title")) | color(theme::Muted())));
    Elements recent_rows;
    for (std::size_t i = 0; i < state->recent_projects.size(); ++i) {
      recent_rows.push_back(render_recent_row(state, layout_state, static_cast<int>(i)));
    }
    body.push_back(vbox(std::move(recent_rows)) | reflect(state->recent_list_box));
    body.push_back(centered_row(text(i18n::tr("welcome.recent.hint")) | color(theme::Muted())));
  }

  body.push_back(text(""));
  body.push_back(
      centered_row(text(i18n::tr("welcome.shortcuts_hint")) | color(theme::Muted())));

  Element inner = vbox(std::move(body));
  const std::string hline(46, '-');
  Element framed = vbox({
                    text("╭" + hline + "╮") | color(theme::AccentDim()),
                    hbox({text("│") | color(theme::AccentDim()), inner | flex,
                          text("│") | color(theme::AccentDim())}),
                    text("╰" + hline + "╯") | color(theme::AccentDim()),
                });

  Element footer = vbox({
      text(i18n::tr("welcome.author")) | color(theme::Muted()),
      text(i18n::tr("welcome.email")) | color(theme::Muted()),
      text(i18n::tr("welcome.license")) | color(theme::Muted()),
  });

  return vbox({
             filler(),
             centered_row(std::move(framed)),
             filler(),
             hbox({filler(), std::move(footer), text("  ")}),
         }) |
         flex | bgcolor(theme::PanelBg());
}

}  // namespace

Element RenderTuideLogo() {
  static const std::vector<std::pair<std::string, Color>> lines = {
      {"████████╗██╗   ██╗██╗██████╗ ███████╗", theme::Accent()},
      {"╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝", theme::TitleText()},
      {"   ██║   ██║   ██║██║██║  ██║█████╗  ", theme::Accent()},
      {"   ██║   ██║   ██║██║██║  ██║██╔══╝  ", theme::TitleText()},
      {"   ██║   ╚██████╔╝██║██████╔╝███████╗", theme::Accent()},
      {"   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝", theme::TitleText()},
  };
  Elements logo_rows;
  for (const auto& line : lines) {
    logo_rows.push_back(text(line.first) | color(line.second) | bold);
  }
  return vbox(std::move(logo_rows));
}

Component MakeWelcomeScreen(MainLayoutState* layout_state, WelcomeScreenState* state,
                            std::function<void()> on_external_file,
                            std::function<void()> on_debug,
                            std::function<void()> on_workspace,
                            std::function<void(const std::string&)> on_recent_project) {
  if (layout_state != nullptr) {
    layout_state->welcome_mouse_handler =
        [layout_state, state, on_external_file, on_debug, on_workspace,
         on_recent_project](Event& event) {
          if (!layout_state->welcome_visible || state == nullptr || !event.is_mouse()) {
            return false;
          }
          if (event.mouse().motion == Mouse::Moved) {
            return update_welcome_hover(state, layout_state, event.mouse().x, event.mouse().y);
          }
          return handle_welcome_mouse(state, layout_state, event.mouse(), on_external_file,
                                      on_debug, on_workspace, on_recent_project);
        };
    layout_state->welcome_key_handler =
        [layout_state, state, on_external_file, on_debug, on_workspace,
         on_recent_project](Event& event) {
          if (layout_state == nullptr || !layout_state->welcome_visible || event.is_mouse()) {
            return false;
          }
          if (event == Event::F1) {
            trigger_press(layout_state, press_id::kWelcomeExternalFile);
            if (on_external_file) {
              on_external_file();
            }
            return true;
          }
          if (event == Event::F2) {
            trigger_press(layout_state, press_id::kWelcomeDebug);
            if (on_debug) {
              on_debug();
            }
            return true;
          }
          if (event == Event::F3) {
            trigger_press(layout_state, press_id::kWelcomeWorkspace);
            if (on_workspace) {
              on_workspace();
            }
            return true;
          }
          if (state != nullptr && !state->recent_projects.empty()) {
            if (event == Event::ArrowDown || event == Event::Character('j')) {
              if (move_recent_selection(state, 1)) {
                UI_WAKE(layout_state, "wake");
              }
              return true;
            }
            if (event == Event::ArrowUp || event == Event::Character('k')) {
              if (move_recent_selection(state, -1)) {
                UI_WAKE(layout_state, "wake");
              }
              return true;
            }
            if (event == Event::Return) {
              if (state->selected_recent < 0) {
                state->selected_recent = 0;
              }
              return open_recent_at(state, layout_state, state->selected_recent,
                                    on_recent_project);
            }
            for (int digit = 1; digit <= 5; ++digit) {
              if (event == Event::Character(static_cast<char>('0' + digit))) {
                return open_recent_at(state, layout_state, digit - 1, on_recent_project);
              }
            }
          }
          return false;
        };
  }

  return Renderer([layout_state, state] {
    if (layout_state == nullptr || !layout_state->welcome_visible || state == nullptr) {
      return text("") | flex;
    }
    return render_welcome_screen(state, layout_state);
  });
}

}  // namespace tuide
