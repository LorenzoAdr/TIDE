#pragma once

#include <memory>
#include <optional>
#include <string>

#include "app/debug_model.hpp"
#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"
#include "terminal/shell_session.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"
#include "util/nm_reader.hpp"

namespace tgdb {

void request_binary_symbols_panel(MainLayoutState* layout_state, const std::string& binary_path,
                                  const std::string& name_filter = {},
                                  NmBindingFilter binding_filter = NmBindingFilter::kAll,
                                  bool open_tab = true);

void refresh_binary_symbols_if_matches(MainLayoutState* layout_state, const std::string& binary_path);

void scan_shell_output_for_linker_errors(const std::string& output, MainLayoutState* layout_state,
                                         DebugModel* model);

ftxui::Component MakeBinarySymbolsPanel(WorkspaceModel* workspace, DebugModel* model,
                                        FocusManagerState* focus, MainLayoutState* layout_state,
                                        const std::shared_ptr<ISymbolProvider>& symbols,
                                        SymbolWorkspaceIndexer* symbol_indexer,
                                        WorkspaceIndexer* file_indexer,
                                        ShellSession* shell);

}  // namespace tgdb
