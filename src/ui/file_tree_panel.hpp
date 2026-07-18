#pragma once

#include <functional>
#include <memory>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

using CommandCallback = std::function<void(const struct UiCommand&)>;

ftxui::Component MakeFileTreePanel(DebugModel* model, WorkspaceModel* workspace,
                                   FocusManagerState* focus,
                                   WorkspaceIndexer* indexer,
                                   CommandCallback on_command,
                                   MainLayoutState* layout_state,
                                   GitService* git_service);

}  // namespace tuide
