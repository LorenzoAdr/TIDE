#pragma once

#include <array>
#include <set>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "git/git_service.hpp"
#include "ui/focus_manager.hpp"
#include "ui/scroll_bar.hpp"

namespace tuide {

struct MainLayoutState;
struct WorkspaceModel;

struct GitPanelState {
  static constexpr int kTabStatus = 0;
  static constexpr int kTabLog = 1;
  static constexpr int kTabBranches = 2;
  static constexpr int kTabTimeline = 3;
  static constexpr int kTabGraph = 4;

  int selected_tab = kTabStatus;
  int selected_file = 0;
  int file_scroll = 0;
  int log_scroll = 0;
  int branch_scroll = 0;
  int timeline_scroll = 0;
  int graph_scroll = 0;
  int diff_scroll = 0;
  int last_list_visible = 1;
  int last_list_total = 0;
  int last_diff_visible = 1;
  int last_diff_total = 0;
  int file_list_width = 0;  // 0 = auto (1/3 del panel) hasta que el usuario arrastre
  bool file_list_width_custom = false;
  bool tree_view = false;
  bool list_sep_hovered = false;
  bool list_sep_dragging = false;
  int list_sep_drag_start_x = 0;
  int list_sep_drag_start_width = 0;
  bool list_scrollbar_dragging = false;
  int list_scrollbar_drag_offset = 0;
  bool diff_scrollbar_dragging = false;
  int diff_scrollbar_drag_offset = 0;
  // Índices en GitStatusSnapshot::entries (multi-selección con Ctrl+clic).
  std::set<int> multi_selected;
  std::string last_diff_path;
  std::string timeline_file;
  std::string last_timeline_commit;
  std::string last_log_commit;
  std::string search_query;
  int search_cursor = 0;
  bool search_focus = false;
  std::string log_search_applied;
  std::string commit_message;
  int commit_cursor = 0;
  int commit_modal_file_scroll = 0;
  std::string status_message;
  bool commit_modal_open = false;
  bool operation_pending = false;
  std::string pending_diff_path;
  int last_file_click_index = -1;
  int64_t last_file_click_ms = 0;

  ftxui::Box panel_box;
  std::array<ftxui::Box, 5> tab_boxes{};
  ftxui::Box file_list_box;
  ftxui::Box list_scrollbar_box;
  ftxui::Box diff_box;
  ftxui::Box diff_scrollbar_box;
  ftxui::Box list_sep_box;
  ScrollbarLayout list_scrollbar_layout{};
  ScrollbarLayout diff_scrollbar_layout{};
  ftxui::Box view_toggle_box;
  ftxui::Box search_box;
  ftxui::Box stage_box;
  ftxui::Box unstage_box;
  ftxui::Box discard_box;
  ftxui::Box push_box;
  ftxui::Box pull_box;
  ftxui::Box commit_box;
  ftxui::Box commit_modal_confirm_box;
  ftxui::Box commit_modal_cancel_box;
  ftxui::Box commit_modal_files_box;
};

ftxui::Component MakeGitPanel(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                              FocusManagerState* focus, WorkspaceModel* workspace,
                              int* content_height = nullptr);

// Overlay a pantalla completa para centrar el modal de commit como el resto.
ftxui::Component MakeGitCommitModalOverlay(ftxui::Component main, GitService* git,
                                           GitPanelState* state, MainLayoutState* layout_state);

void GitPanelEnsureSelectedDiff(GitService* git, GitPanelState* state);

void GitPanelActivate(GitService* git, GitPanelState* state);

}  // namespace tuide
