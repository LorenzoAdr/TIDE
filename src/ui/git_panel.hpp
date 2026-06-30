#pragma once

#include <array>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "git/git_service.hpp"
#include "ui/focus_manager.hpp"

namespace tgdb {

struct MainLayoutState;

struct GitPanelState {
  static constexpr int kTabStatus = 0;
  static constexpr int kTabLog = 1;
  static constexpr int kTabBranches = 2;

  int selected_tab = kTabStatus;
  int selected_file = 0;
  int file_scroll = 0;
  int log_scroll = 0;
  int branch_scroll = 0;
  std::string commit_message;
  int commit_cursor = 0;
  std::string status_message;
  bool commit_input_focus = false;
  bool operation_pending = false;
  std::string pending_diff_path;

  ftxui::Box panel_box;
  std::array<ftxui::Box, 3> tab_boxes{};
  ftxui::Box file_list_box;
  ftxui::Box stage_box;
  ftxui::Box unstage_box;
  ftxui::Box push_box;
  ftxui::Box pull_box;
  ftxui::Box commit_box;
};

ftxui::Component MakeGitPanel(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                              FocusManagerState* focus);

void GitPanelEnsureSelectedDiff(GitService* git, GitPanelState* state);

}  // namespace tgdb
