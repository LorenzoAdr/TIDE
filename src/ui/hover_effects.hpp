#pragma once

#include <string_view>

#include "ui/main_layout.hpp"

namespace tgdb {

// Temporary kill-switch while profiling mouse-move CPU usage.
inline constexpr bool kHoverEffectsEnabled = true;

inline bool hover_effects_enabled() { return kHoverEffectsEnabled; }

// LSP symbol tooltip in the editor: independent of chrome hover styling.
inline constexpr bool kEditorLspHoverEnabled = true;
inline bool editor_lsp_hover_enabled() { return kEditorLspHoverEnabled; }

inline bool hover_state_changed(std::string_view before, std::string_view after) {
  return hover_effects_enabled() && before != after;
}

inline bool apply_hover_repaint(MainLayoutState* layout, std::string_view before) {
  if (layout == nullptr || !hover_effects_enabled()) {
    return false;
  }
  if (layout->clickable.hovered_id() != before) {
    layout->request_ui_tick = true;
    return true;
  }
  return false;
}

}  // namespace tgdb
