#include "ui/welcome_screen.hpp"

#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/press_ids.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct LogoLine {
  std::string text;
  Color color;
};

const std::vector<LogoLine>& welcome_logo_lines() {
  static const std::vector<LogoLine> lines = {
      {"████████╗██╗   ██╗██╗██████╗ ███████╗", theme::Accent()},
      {"╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝", theme::TitleText()},
      {"   ██║   ██║   ██║██║██║  ██║█████╗  ", theme::Accent()},
      {"   ██║   ██║   ██║██║██║  ██║██╔══╝  ", theme::TitleText()},
      {"   ██║   ╚██████╔╝██║██████╔╝███████╗", theme::Accent()},
      {"   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝", theme::TitleText()},
  };
  return lines;
}

Element centered_row(Element row) {
  return hbox({filler(), std::move(row), filler()});
}

Element render_welcome_logo() {
  Elements logo_rows;
  for (const auto& line : welcome_logo_lines()) {
    logo_rows.push_back(text(line.text) | color(line.color) | bold);
  }
  return vbox(std::move(logo_rows));
}

Element render_action_row(const std::string& label, Box* box, MainLayoutState* layout,
                          std::string_view id) {
  const bool hovered = layout != nullptr && layout->clickable.is_hovered(id);
  const bool pressed = layout != nullptr && layout->clickable.is_pressed(id);
  Element row = text("  ▸  " + label) | color(theme::Header());
  if (pressed) {
    row = row | inverted | bold | bgcolor(theme::TabPressed());
  } else if (hovered) {
    row = row | bold | bgcolor(theme::TabHover());
  }
  return row | size(WIDTH, EQUAL, 44) | reflect(*box);
}

bool update_welcome_hover(WelcomeScreenState* state, MainLayoutState* layout_state, int x,
                          int y) {
  if (state == nullptr || layout_state == nullptr) {
    return false;
  }
  return update_panel_hover(
      layout_state, x, y,
      {{press_id::kWelcomeExternalFile, &state->external_file_action_box},
       {press_id::kWelcomeDebug, &state->debug_action_box},
       {press_id::kWelcomeWorkspace, &state->workspace_action_box}},
      press_id::is_welcome_hover);
}

bool handle_welcome_mouse(WelcomeScreenState* state, MainLayoutState* layout_state,
                          const Mouse& m, const std::function<void()>& on_external_file,
                          const std::function<void()>& on_debug,
                          const std::function<void()>& on_workspace) {
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
  return false;
}

Element render_welcome_screen(WelcomeScreenState* state, MainLayoutState* layout_state) {
  Elements body;
  body.push_back(render_welcome_logo());
  body.push_back(text(""));
  body.push_back(
      centered_row(text("depura y edita desde la terminal") | color(theme::Muted())));
  body.push_back(text(""));
  body.push_back(render_action_row("F1   Abrir archivo suelto", &state->external_file_action_box,
                                   layout_state, press_id::kWelcomeExternalFile));
  body.push_back(text(""));
  body.push_back(render_action_row("F2   Iniciar depuración", &state->debug_action_box,
                                   layout_state, press_id::kWelcomeDebug));
  body.push_back(text(""));
  body.push_back(render_action_row("F3   Abrir workspace", &state->workspace_action_box,
                                   layout_state, press_id::kWelcomeWorkspace));
  body.push_back(text(""));
  body.push_back(
      centered_row(text("Alt+F1 / Shift+F1 atajos de teclado") | color(theme::Muted())));

  Element inner = vbox(std::move(body));
  const std::string hline(46, '-');
  Element framed = vbox({
                    text("╭" + hline + "╮") | color(theme::AccentDim()),
                    hbox({text("│") | color(theme::AccentDim()), inner | flex,
                          text("│") | color(theme::AccentDim())}),
                    text("╰" + hline + "╯") | color(theme::AccentDim()),
                });

  Element footer = vbox({
      render_welcome_logo(),
      text(""),
      text("Lorenzo Arias del Real") | color(theme::Muted()),
      text("lorenzo.adr@proton.me") | color(theme::Muted()),
      text("Apache License 2.0") | color(theme::Muted()),
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

Component MakeWelcomeScreen(MainLayoutState* layout_state, WelcomeScreenState* state,
                            std::function<void()> on_external_file,
                            std::function<void()> on_debug,
                            std::function<void()> on_workspace) {
  if (layout_state != nullptr) {
    layout_state->welcome_mouse_handler = [layout_state, state, on_external_file, on_debug,
                                           on_workspace](Event& event) {
      if (!layout_state->welcome_visible || state == nullptr || !event.is_mouse()) {
        return false;
      }
      if (event.mouse().motion == Mouse::Moved) {
        return update_welcome_hover(state, layout_state, event.mouse().x, event.mouse().y);
      }
      return handle_welcome_mouse(state, layout_state, event.mouse(), on_external_file, on_debug,
                                  on_workspace);
    };
    layout_state->welcome_key_handler = [layout_state, on_external_file, on_debug,
                                         on_workspace](Event& event) {
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

}  // namespace tgdb
