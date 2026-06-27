#pragma once

#include <functional>
#include <memory>

#include "app/app_mode.hpp"
#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "ui/source_panel.hpp"

namespace tgdb {

using CommandCallback = std::function<void(const struct UiCommand&)>;

enum class TextInputFocus {
  None,
  Console,
  Watch,
  WatchInject,
};

using StopDebugCallback = std::function<void()>;

struct MainLayoutState {
  bool console_visible = true;
  TextInputFocus text_input_focus = TextInputFocus::None;
  bool focus_sync_needed = false;
};

ftxui::Component MakeMainLayout(AppMode* app_mode, DebugModel* model,
                                WorkspaceModel* workspace, SourceViewState* source_state,
                                FocusManagerState* focus,
                                std::shared_ptr<ISymbolProvider> symbols,
                                CommandCallback on_command,
                                MainLayoutState* layout_state,
                                StopDebugCallback on_stop_debug = {});

}  // namespace tgdb
