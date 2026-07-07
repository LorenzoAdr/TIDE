#pragma once

#include <string_view>

#include "ui/main_layout.hpp"

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

inline bool chrome_hover_allowed(const MainLayoutState* layout) {
  return hover_effects_enabled() &&
         (layout == nullptr || layout->activity_gate.allows_hover_chrome());
}

inline bool editor_scope_effects_allowed(const MainLayoutState* layout,
                                         bool scope_highlight_enabled) {
  return editor_scope_effects_enabled(scope_highlight_enabled) &&
         (layout == nullptr || layout->activity_gate.allows_hover_chrome());
}

inline bool apply_hover_repaint(MainLayoutState* layout, std::string_view before) {
  if (layout == nullptr || !chrome_hover_allowed(layout)) {
    return false;
  }
  if (layout->clickable.hovered_id() != before) {
    layout->request_ui_tick = true;
    return true;
  }
  return false;
}

}  // namespace tgdb
