#include "ui/console_expand_overlay.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/ui_wake.hpp"

namespace tuide {

using namespace ftxui;

namespace {

bool collapse_console_expand(MainLayoutState* layout_state) {
  if (layout_state == nullptr || !layout_state->console_expanded) {
    return false;
  }
  layout_state->console_expanded = false;
  layout_state->panel_render_cache.mark_dirty(UiPanelId::Console);
  UI_WAKE(layout_state, "console.collapse");
  return true;
}

}  // namespace

Component MakeConsoleExpandOverlay(Component main, MainLayoutState* layout_state) {
  return Renderer(
      CatchEvent(main, [layout_state](Event event) {
        if (layout_state == nullptr || !layout_state->console_expanded) {
          return false;
        }
        if (event == Event::Escape) {
          // Esc must collapse the expand modal; do not let terminal/console handlers
          // swallow Escape (they clear text_input_focus and return true).
          return collapse_console_expand(layout_state);
        }
        if (event.is_mouse() && layout_state->console_mouse_handler &&
            layout_state->console_mouse_handler(event)) {
          return true;
        }
        return false;
      }),
      [main, layout_state] {
        Element base = main->Render();
        if (layout_state == nullptr || !layout_state->console_expanded ||
            !layout_state->console_visible || !layout_state->console_expanded_render) {
          return base;
        }
        Element dialog = layout_state->console_expanded_render();
        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tuide
