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
  bool symbols_fetch_pending = false;
  int selected = 0;
  Box content_box;
};

void fetch_outline_symbols(OutlinePanelState* state, ISymbolProvider* symbols,
                           WorkspaceModel* workspace) {
  if (state == nullptr || symbols == nullptr || !state->symbols_fetch_pending ||
      state->loaded_file.empty()) {
    return;
  }
  state->symbols_fetch_pending = false;
  state->symbols = symbols->symbols_for_file(state->loaded_file);
  if (workspace != nullptr) {
    workspace->buffer.view_token++;
  }
}

}  // namespace

Component MakeOutlinePanel(WorkspaceModel* workspace, FocusManagerState* focus,
                           std::shared_ptr<ISymbolProvider> symbols,
                           MainLayoutState* layout_state) {
  auto state = std::make_shared<OutlinePanelState>();

  if (layout_state != nullptr) {
    layout_state->outline_tick_callback = [state, symbols, workspace]() {
      fetch_outline_symbols(state.get(), symbols.get(), workspace);
    };
  }

  auto renderer = Renderer([workspace, focus, state] {
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    if (path != state->loaded_file) {
      state->loaded_file = path;
      state->symbols.clear();
      state->symbols_fetch_pending = !path.empty();
      state->selected = 0;
    }

    Elements rows;
    if (state->symbols_fetch_pending && state->symbols.empty()) {
      rows.push_back(text("(cargando…)") | color(theme::Muted()));
    } else if (state->symbols.empty()) {
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

  return WrapFocusable(CatchEvent(renderer, [workspace, focus, state, layout_state](Event event) {
    if (layout_state != nullptr &&
        (is_watch_input_focus(layout_state->text_input_focus) ||
         layout_state->right_panel_active_section == 1)) {
      return false;
    }

    if (event.is_mouse() && event.mouse().button == Mouse::Left &&
        event.mouse().motion == Mouse::Pressed) {
      const auto& m = event.mouse();
      if (state->content_box.Contain(m.x, m.y)) {
        if (layout_state != nullptr) {
          layout_state->right_panel_active_section = 0;
        }
        focus->region = FocusRegion::RightPanel;
        const int row = m.y - state->content_box.y_min;
        if (row >= 0 && row < static_cast<int>(state->symbols.size())) {
          state->selected = row;
          const auto& sym = state->symbols[row];
          workspace->record_cursor_jump();
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
      workspace->record_cursor_jump();
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
