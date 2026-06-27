#pragma once

#include <memory>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

ftxui::Component MakeOutlinePanel(WorkspaceModel* workspace, FocusManagerState* focus,
                                  std::shared_ptr<ISymbolProvider> symbols);

}  // namespace tgdb
