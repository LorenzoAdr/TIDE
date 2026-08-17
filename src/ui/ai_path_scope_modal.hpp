#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ftxui/component/component_base.hpp"
#include "ui/main_layout.hpp"
#include "ui/path_browser.hpp"

namespace tuide {

enum class AiPathScopePanel {
  kList,
  kBrowser,
};

struct AiPathScopeModalState {
  bool open = false;
  AiPathScopePanel panel = AiPathScopePanel::kList;
  std::vector<std::string> draft_paths;
  int selected = 0;
  PathBrowserState path_browser;
  std::string workspace_root;
};

void open_ai_path_scope_modal(AiPathScopeModalState* state, const std::string& workspace_root,
                              const std::vector<std::string>& current_paths);

using AiPathScopeApplyCallback = std::function<void(std::vector<std::string> paths)>;

ftxui::Component MakeAiPathScopeModalOverlay(ftxui::Component main, AiPathScopeModalState* state,
                                             MainLayoutState* layout_state,
                                             AiPathScopeApplyCallback on_apply);

}  // namespace tuide
