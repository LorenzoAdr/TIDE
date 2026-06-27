#include "ui/symbol_picker.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool contains_insensitive(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

}  // namespace

void SymbolPickerState::sync_symbols(const WorkspaceModel& workspace,
                                     const std::shared_ptr<ISymbolProvider>& symbols) {
  const std::string path =
      workspace.buffer.path.empty() ? workspace.active_file : workspace.buffer.path;
  if (path == loaded_file) {
    return;
  }
  loaded_file = path;
  all_symbols = symbols ? symbols->symbols_for_file(path) : std::vector<SymbolInfo>{};
  refresh_matches();
}

void SymbolPickerState::refresh_matches() {
  matches.clear();
  for (const auto& sym : all_symbols) {
    if (contains_insensitive(sym.name, query)) {
      matches.push_back(sym);
    }
  }
  if (selected >= static_cast<int>(matches.size())) {
    selected = std::max(0, static_cast<int>(matches.size()) - 1);
  }
}

void SymbolPickerState::jump_to_selected(WorkspaceModel* workspace, FocusManagerState* focus) {
  if (matches.empty() || workspace == nullptr) {
    return;
  }
  selected = std::max(0, std::min(selected, static_cast<int>(matches.size()) - 1));
  const auto& sym = matches[static_cast<std::size_t>(selected)];
  workspace->buffer.reset_to_single_cursor(std::max(0, sym.line - 1), 0);
  workspace->buffer.scroll = std::max(0, workspace->buffer.primary_line() - 2);
  workspace->buffer.view_token++;
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  open = false;
  query.clear();
  selected = 0;
  refresh_matches();
}

Component MakeSymbolPickerOverlay(Component main, WorkspaceModel* workspace,
                                  SymbolPickerState* state, FocusManagerState* focus,
                                  std::shared_ptr<ISymbolProvider> symbols) {
  return Renderer(
      CatchEvent(main, [workspace, state, focus, symbols](Event event) {
        if (!state->open) {
          return false;
        }

        state->sync_symbols(*workspace, symbols);

        if (event == Event::Escape) {
          state->open = false;
          state->query.clear();
          state->selected = 0;
          state->refresh_matches();
          return true;
        }
        if (event == Event::Return) {
          state->jump_to_selected(workspace, focus);
          return true;
        }
        if (event == Event::ArrowDown) {
          if (!state->matches.empty()) {
            state->selected = std::min(state->selected + 1,
                                       static_cast<int>(state->matches.size()) - 1);
          }
          return true;
        }
        if (event == Event::ArrowUp) {
          state->selected = std::max(0, state->selected - 1);
          return true;
        }
        if (event == Event::CtrlO) {
          if (!state->matches.empty()) {
            state->selected =
                (state->selected + 1) % static_cast<int>(state->matches.size());
          }
          return true;
        }
        if (event == Event::Backspace) {
          if (!state->query.empty()) {
            state->query.pop_back();
            state->selected = 0;
            state->refresh_matches();
          }
          return true;
        }
        if (event.is_character()) {
          const std::string ch = event.character();
          if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
              static_cast<unsigned char>(ch[0]) < 127) {
            state->query += ch;
            state->selected = 0;
            state->refresh_matches();
          }
          return true;
        }
        return true;
      }),
      [main, workspace, state, symbols] {
        Element base = main->Render();
        if (!state->open) {
          return base;
        }

        state->sync_symbols(*workspace, symbols);

        std::string input_line = state->query;
        input_line.push_back('_');

        Elements matches;
        const int max_rows = 14;
        const int start = std::max(
            0, std::min(state->selected,
                        std::max(0, static_cast<int>(state->matches.size()) - max_rows)));
        const int end =
            std::min(static_cast<int>(state->matches.size()), start + max_rows);
        for (int i = start; i < end; ++i) {
          const auto& sym = state->matches[static_cast<std::size_t>(i)];
          std::string label = sym.name + "  :" + std::to_string(sym.line);
          Element row = text(label) | color(theme::Header());
          if (i == state->selected) {
            row = row | inverted | bold;
          }
          matches.push_back(row);
        }
        if (matches.empty()) {
          matches.push_back(text("(sin coincidencias)") | color(theme::Muted()));
        }

        Element dialog = ModalWindow(
            text("Ir a símbolo") | color(theme::Accent()),
            vbox({ModalInputLine(input_line),
                  separator(),
                  vbox(std::move(matches)) | frame | vscroll_indicator |
                      bgcolor(theme::PanelBg())}));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
