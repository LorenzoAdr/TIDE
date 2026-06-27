#include "ui/outline_panel.hpp"

#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focusable_component.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct OutlinePanelState {
  std::vector<SymbolInfo> symbols;
  std::string loaded_file;
  int selected = 0;
  Box content_box;
};

}  // namespace

Component MakeOutlinePanel(WorkspaceModel* workspace, FocusManagerState* focus,
                           std::shared_ptr<ISymbolProvider> symbols) {
  auto state = std::make_shared<OutlinePanelState>();

  auto renderer = Renderer([workspace, focus, state, symbols] {
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    if (path != state->loaded_file) {
      state->loaded_file = path;
      state->symbols = symbols ? symbols->symbols_for_file(path) : std::vector<SymbolInfo>{};
      state->selected = 0;
    }

    Elements rows;
    if (state->symbols.empty()) {
      rows.push_back(text("(sin símbolos)") | color(theme::Muted()));
    } else {
      for (int i = 0; i < static_cast<int>(state->symbols.size()); ++i) {
        const auto& sym = state->symbols[i];
        std::string indent(static_cast<std::size_t>(sym.depth * 2), ' ');
        Element row = text(indent + sym.name);
        if (i == state->selected && focus->region == FocusRegion::RightPanel) {
          row = row | inverted | bold;
        } else {
          row = row | color(theme::Header());
        }
        rows.push_back(row);
      }
    }

    auto content = vbox(std::move(rows)) | vscroll_indicator | frame | flex |
                   reflect(state->content_box) | bgcolor(theme::PanelBg());
    return PanelBody(std::move(content));
  });

  return WrapFocusable(CatchEvent(renderer, [workspace, focus, state](Event event) {
    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (state->content_box.Contain(m.x, m.y)) {
        focus->region = FocusRegion::RightPanel;
        const int row = m.y - state->content_box.y_min;
        if (row >= 0 && row < static_cast<int>(state->symbols.size())) {
          state->selected = row;
          const auto& sym = state->symbols[row];
          workspace->buffer.reset_to_single_cursor(std::max(0, sym.line - 1), 0);
          workspace->buffer.scroll = std::max(0, workspace->buffer.primary_line() - 2);
          workspace->buffer.view_token++;
          focus->region = FocusRegion::Editor;
        }
        return true;
      }
      return false;
    }

    if (focus->region != FocusRegion::RightPanel) {
      return false;
    }
    if (state->symbols.empty()) {
      return false;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      state->selected = std::min(state->selected + 1,
                                 static_cast<int>(state->symbols.size()) - 1);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      state->selected = std::max(0, state->selected - 1);
      return true;
    }
    if (event == Event::Return) {
      const auto& sym = state->symbols[state->selected];
      workspace->buffer.reset_to_single_cursor(std::max(0, sym.line - 1), 0);
      workspace->buffer.scroll = std::max(0, workspace->buffer.primary_line() - 2);
      workspace->buffer.view_token++;
      focus->region = FocusRegion::Editor;
      return true;
    }
    return false;
  }));
}

}  // namespace tgdb
