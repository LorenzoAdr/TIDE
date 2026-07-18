#pragma once

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

ftxui::Component MakeCallHierarchyPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                        MainLayoutState* layout_state,
                                        RightSidebarState* sidebar,
                                        const std::shared_ptr<ISymbolProvider>& symbols);

}  // namespace tuide
