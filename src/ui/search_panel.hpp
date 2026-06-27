#pragma once

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

ftxui::Component MakeSearchPanel(WorkspaceModel* workspace, DebugModel* model,
                                 FocusManagerState* focus, MainLayoutState* layout_state,
                                 WorkspaceIndexer* indexer, RightSidebarState* sidebar);

}  // namespace tgdb
