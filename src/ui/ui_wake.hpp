#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "ui/main_layout.hpp"
#include "ui/ui_event_dispatcher.hpp"
#include "ui/ui_event_types.hpp"

namespace tuide {

inline UiEventDispatcher* ui_event_dispatcher(MainLayoutState* layout) {
  return layout != nullptr ? layout->ui_events : nullptr;
}

inline uint64_t ui_capture_correlation(MainLayoutState* layout) {
  UiEventDispatcher* dispatcher = ui_event_dispatcher(layout);
  if (dispatcher == nullptr) {
    return 0;
  }
  return dispatcher->current_correlation_id();
}

inline void ui_wake(MainLayoutState* layout, std::string_view tag,
                    UiEventKind kind = UiEventKind::InputCorrelated,
                    std::function<void()> pre_paint = {}) {
  UiEventDispatcher* dispatcher = ui_event_dispatcher(layout);
  if (dispatcher == nullptr) {
    if (pre_paint) {
      pre_paint();
    }
    return;
  }
  UiEvent event;
  event.kind = kind;
  event.correlation_id = dispatcher->current_correlation_id();
  event.tag = std::string(tag);
  event.pre_paint = std::move(pre_paint);
  event.src_file = __FILE__;
  event.src_line = __LINE__;
  dispatcher->emit(std::move(event));
}

// Console is cached: any tab/content change must dirty it before wake/draw.
inline void wake_console_panel(MainLayoutState* layout, std::string_view tag = "wake") {
  if (layout != nullptr) {
    layout->panel_render_cache.mark_dirty(UiPanelId::Console);
  }
  ui_wake(layout, tag);
}

// Streaming console output (AI transcript, compile logs): refresh the console
// without InputCorrelated's full background plan (index/git/editor ticks).
// Using TerminalOutput + animation frame matches PTY streaming behaviour.
inline void wake_console_panel_stream(MainLayoutState* layout, std::string_view tag = "wake") {
  if (layout != nullptr) {
    layout->panel_render_cache.mark_dirty(UiPanelId::Console);
  }
  UiEventDispatcher* dispatcher = ui_event_dispatcher(layout);
  if (dispatcher == nullptr) {
    return;
  }
  dispatcher->emit_terminal(std::string(tag), {}, __FILE__, __LINE__);
}

inline void ui_wake_correlated(MainLayoutState* layout, uint64_t correlation_id,
                               std::string_view tag, std::function<void()> pre_paint = {}) {
  UiEventDispatcher* dispatcher = ui_event_dispatcher(layout);
  if (dispatcher == nullptr) {
    if (pre_paint) {
      pre_paint();
    }
    return;
  }
  dispatcher->emit_correlated_if_valid(correlation_id, std::string(tag), std::move(pre_paint),
                                       __FILE__, __LINE__);
}

#define UI_WAKE(layout, tag) ui_wake((layout), (tag))
#define UI_WAKE_FN(layout, tag, fn) ui_wake((layout), (tag), UiEventKind::InputCorrelated, (fn))

}  // namespace tuide
