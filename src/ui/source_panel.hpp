#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

struct SourceViewState {
  std::vector<std::string> lines;
  int scroll = 0;
};

void ToggleBreakpointAtLine(DebugModel* model, int line, CommandCallback on_command);

ftxui::Component MakeSourcePanel(DebugModel* model, SourceViewState* view_state,
                                 CommandCallback on_command);

}  // namespace tgdb
