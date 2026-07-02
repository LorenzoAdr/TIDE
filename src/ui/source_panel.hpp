#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"

#include "ui/focus_manager.hpp"

namespace tgdb {

struct MainLayoutState;

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct SourceViewState {
  std::vector<std::string> lines;
  int scroll = 0;
};

void ToggleBreakpointAtFile(DebugModel* model, const std::string& file, int line,
                            CommandCallback on_command);
void ToggleBreakpointAtLine(DebugModel* model, int line, CommandCallback on_command);

ftxui::Component MakeSourcePanel(DebugModel* model, SourceViewState* view_state,
                                 CommandCallback on_command,
                                 FocusManagerState* focus, MainLayoutState* layout_state);

}  // namespace tgdb
