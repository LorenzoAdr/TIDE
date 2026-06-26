#pragma once

#include <functional>

#include "ftxui/component/component_base.hpp"
#include "app/debug_model.hpp"
#include "ui/source_panel.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;
using TickCallback = std::function<void()>;

struct MainLayoutState {
  bool console_visible = true;
};

ftxui::Component MakeMainLayout(DebugModel* model, SourceViewState* source_state,
                                CommandCallback on_command,
                                MainLayoutState* layout_state,
                                TickCallback on_tick = {});

}  // namespace tgdb
