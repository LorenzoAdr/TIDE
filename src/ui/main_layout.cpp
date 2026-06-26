#include "ui/main_layout.hpp"

#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/console_panel.hpp"
#include "ui/file_tree_panel.hpp"
#include "ui/panel.hpp"
#include "ui/source_panel.hpp"
#include "ui/theme.hpp"
#include "ui/watches_panel.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct LayoutState {
  int left_width = 22;
  int right_width = 46;
  int bottom_height = 8;
};

Component MakeVSplitLeft(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Left;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorVertical(); };
  return ResizableSplit(std::move(options));
}

Component MakeVSplitRight(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Right;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorVertical(); };
  return ResizableSplit(std::move(options));
}

Component MakeHSplitBottom(Component main, Component back, int* main_size) {
  ResizableSplitOption options;
  options.main = std::move(main);
  options.back = std::move(back);
  options.direction = Direction::Down;
  options.main_size = main_size;
  options.separator_func = [] { return SplitSeparatorHorizontal(); };
  return ResizableSplit(std::move(options));
}

}  // namespace

Component MakeMainLayout(DebugModel* model, SourceViewState* source_state,
                         CommandCallback on_command,
                         MainLayoutState* layout_state, TickCallback on_tick) {
  auto split_state = std::make_shared<LayoutState>();

  auto file_tree = MakeFileTreePanel(model, on_command);
  auto source = MakeSourcePanel(model, source_state, on_command);
  auto watches = MakeWatchesPanel(model, on_command);
  auto console = MakeConsolePanel(model, on_command);

  auto explorer_and_source = MakeVSplitLeft(file_tree, source, &split_state->left_width);

  auto workspace =
      MakeVSplitRight(watches, explorer_and_source, &split_state->right_width);

  auto with_console =
      MakeHSplitBottom(console, workspace, &split_state->bottom_height);

  return Renderer(with_console, [model, workspace, with_console, on_tick, split_state,
                                 layout_state] {
    if (on_tick) {
      on_tick();
    }

    Element main =
        layout_state->console_visible ? with_console->Render() : workspace->Render();

    Element status = hbox({
        text(" tgdb ") | bold | color(theme::Accent()),
        text(" │ "),
        text(model->status_message) | flex | color(theme::Header()),
        text("F5 ▶  F10 step  Ctrl+P  Ctrl+T consola  q ") | color(theme::Muted()),
    }) | bgcolor(theme::StatusBar());

    return vbox({main | flex | bgcolor(theme::PanelBg()), status});
  });
}

}  // namespace tgdb
