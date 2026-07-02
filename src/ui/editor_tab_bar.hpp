#pragma once

#include <string>
#include <vector>

#include "app/workspace_model.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

enum class TabBarHitKind { Tab, Close, Overflow };

struct TabBarHit {
  TabBarHitKind kind = TabBarHitKind::Tab;
  int tab_index = -1;
  ftxui::Box box;
};

struct EditorTabBarState {
  ftxui::Box bar_box;
  std::vector<TabBarHit> hits;
  bool overflow_open = false;
  int overflow_selected = 0;
  bool dragging = false;
  int drag_tab = -1;
  int drag_target = -1;
  int bar_width_chars = 80;
  int hover_tab_index = -1;
  int hover_x = 0;
  int hover_y = 0;
};

ftxui::Element make_editor_tab_bar(WorkspaceModel* workspace, EditorTabBarState* state,
                                   MainLayoutState* layout_state = nullptr);

ftxui::Element make_tabs_overflow_modal(WorkspaceModel* workspace, EditorTabBarState* state);

ftxui::Element make_tab_hover_tooltip(WorkspaceModel* workspace, const EditorTabBarState* state);

bool handle_tab_bar_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                          EditorTabBarState* state, const ftxui::Mouse& m,
                          MainLayoutState* layout_state = nullptr,
                          FocusRegion panel_focus = FocusRegion::Editor);

bool update_editor_chrome_hover(WorkspaceModel* workspace, EditorTabBarState* state,
                                MainLayoutState* layout_state, const ftxui::Box& problems_box, int x,
                                int y);

bool handle_tabs_overflow_keys(WorkspaceModel* workspace, FocusManagerState* focus,
                               EditorTabBarState* state, const ftxui::Event& event,
                               FocusRegion panel_focus = FocusRegion::Editor);

}  // namespace tgdb
