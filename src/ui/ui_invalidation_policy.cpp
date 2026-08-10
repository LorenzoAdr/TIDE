#include "ui/ui_invalidation_policy.hpp"

#include "ui/main_layout.hpp"
#include "ui/ui_wake.hpp"

namespace tuide {

void invalidate(MainLayoutState* layout, UiInvalidation kind, std::string_view tag) {
  const UiInvalidationSpec spec = ui_invalidation_spec(kind);
  if (layout != nullptr) {
    if (spec.focus_sync) {
      layout->focus_sync_needed = true;
    }
    if (spec.panel_mask == kPanelChromeAll) {
      layout->panel_render_cache.mark_all_dirty();
    } else {
      for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(UiPanelId::kCount); ++i) {
        if ((spec.panel_mask & (1u << i)) != 0) {
          layout->panel_render_cache.mark_dirty(static_cast<UiPanelId>(i));
        }
      }
    }
  }
  if (!spec.wake) {
    return;
  }
  const std::string_view wake_tag = tag.empty() ? spec.default_tag : tag;
  ui_wake(layout, wake_tag, spec.kind);
}

}  // namespace tuide
