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
