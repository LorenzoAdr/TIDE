#pragma once

#include <functional>
#include <string>

#include "app/debug_model.hpp"
#include "backend/idebug_backend.hpp"
#include "ftxui/component/component_base.hpp"
#include "ui/main_layout.hpp"
#include "ui/path_browser.hpp"

namespace tgdb {

struct SourceSubstituteModalState {
  bool open = false;
  std::string from_path;
  PathBrowserState browser;
};

using SourceSubstituteApplyCallback =
    std::function<void(const std::string& from_path, const std::string& to_path)>;

void open_source_substitute_modal(SourceSubstituteModalState* state, DebugModel* model,
                                  const std::string& workspace_root);

ftxui::Component MakeSourceSubstituteModalOverlay(
    ftxui::Component main, SourceSubstituteModalState* state, MainLayoutState* layout_state,
    SourceSubstituteApplyCallback on_apply);

}  // namespace tgdb
