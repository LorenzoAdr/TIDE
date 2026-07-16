#pragma once

#include <string_view>
#include "ui/ui_wake.hpp"

#include "ui/main_layout.hpp"
#include "ui/press_ids.hpp"
#include "util/ui_panel_render_cache.hpp"

namespace tgdb {

void set_animations_enabled(bool enabled);
bool animations_enabled();

bool hover_effects_enabled();

// Scope gutter, colored braces, and related editor chrome.
bool editor_scope_effects_enabled(bool scope_highlight_enabled);

// LSP symbol tooltip in the editor: independent of chrome hover styling.
inline constexpr bool kEditorLspHoverEnabled = true;
inline bool editor_lsp_hover_enabled() { return kEditorLspHoverEnabled; }

inline bool hover_state_changed(std::string_view before, std::string_view after) {
  return hover_effects_enabled() && before != after;
}

inline void invalidate_cached_panel_chrome(MainLayoutState* layout, std::string_view id) {
  if (layout == nullptr || id.empty()) {
    return;
  }
  if (press_id::is_explorer_hover(id) || id == press_id::kExplorerHide ||
      id == press_id::kExplorerRefresh) {
    layout->panel_render_cache.mark_dirty(UiPanelId::FileTree);
  }
  if (press_id::is_outline_hover(id) || press_id::is_sidebar_tab_hover(id) ||
      id == press_id::kSidebarHide) {
    layout->panel_render_cache.mark_dirty(UiPanelId::RightSidebar);
  }
}

inline bool chrome_hover_allowed(const MainLayoutState* layout) {
  return hover_effects_enabled() &&
         (layout == nullptr || layout->activity_gate.allows_hover_chrome()) &&
         (layout == nullptr || !layout->activity_gate.is_interactive());
}

inline bool editor_scope_effects_allowed(const MainLayoutState* layout,
                                         bool scope_highlight_enabled) {
  (void)layout;
  return editor_scope_effects_enabled(scope_highlight_enabled);
}

inline bool apply_hover_repaint(MainLayoutState* layout, std::string_view before) {
  if (layout == nullptr || !chrome_hover_allowed(layout)) {
    return false;
  }
  if (layout->clickable.hovered_id() != before) {
    invalidate_cached_panel_chrome(layout, before);
    invalidate_cached_panel_chrome(layout, layout->clickable.hovered_id());
    UI_WAKE(layout, "wake");
    return true;
  }
  return false;
}

}  // namespace tgdb
