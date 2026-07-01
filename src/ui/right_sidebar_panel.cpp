#include "ui/right_sidebar_panel.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

Component MakeRightSidebarPanel(Component outline, MainLayoutState* /*layout_state*/) {
  auto panel = Renderer(outline, [outline] {
    return vbox({
               text(" Outline") | color(theme::Accent()) | bold,
               separator(),
               outline->Render() | flex,
           }) |
           flex | bgcolor(theme::PanelBg());
  });
  return WrapFocusable(std::move(panel));
}

}  // namespace tgdb
