#include "ui/quit_confirm.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kYes = 0;
constexpr int kNo = 1;

Element render_choice(const char* label, bool selected) {
  Element row = text(label) | color(theme::Header());
  if (selected) {
    row = row | inverted | bold;
  }
  return row;
}

}  // namespace

Component MakeQuitConfirmOverlay(Component main, QuitConfirmState* state,
                                 std::function<void()> on_confirm) {
  return Renderer(
      CatchEvent(main, [state, on_confirm](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }

        if (event == Event::Escape) {
          state->open = false;
          state->selected = kYes;
          return true;
        }
        if (event == Event::Return) {
          if (state->selected == kYes) {
            state->open = false;
            if (on_confirm) {
              on_confirm();
            }
          } else {
            state->open = false;
            state->selected = kYes;
          }
          return true;
        }
        if (event == Event::ArrowLeft || event == Event::Character('h')) {
          state->selected = kYes;
          return true;
        }
        if (event == Event::ArrowRight || event == Event::Character('l')) {
          state->selected = kNo;
          return true;
        }
        if (event == Event::Character('s') || event == Event::Character('S')) {
          state->selected = kYes;
          return true;
        }
        if (event == Event::Character('n') || event == Event::Character('N')) {
          state->selected = kNo;
          return true;
        }
        return true;
      }),
      [main, state] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        Element dialog = ModalWindow(
            text("Confirmar salida") | color(theme::Accent()),
            vbox({
                text("¿Salir de tide?") | color(theme::Header()),
                separator(),
                hbox({
                    render_choice(" Sí ", state->selected == kYes),
                    text("  "),
                    render_choice(" No ", state->selected == kNo),
                }),
                text("Enter confirmar  Esc cancelar") | color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
