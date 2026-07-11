#pragma once

#include <functional>
#include <string>

#include "ui/main_layout.hpp"
#include "ui/ui_event_dispatcher.hpp"

namespace tgdb {

class TerminalUiChannel {
 public:
  explicit TerminalUiChannel(MainLayoutState* layout) : layout_(layout) {}

  void on_pty_output(std::function<void()> pre_paint = {});

 private:
  MainLayoutState* layout_;
};

inline void TerminalUiChannel::on_pty_output(std::function<void()> pre_paint) {
  if (layout_ == nullptr || layout_->ui_events == nullptr) {
    if (pre_paint) {
      pre_paint();
    }
    return;
  }
  layout_->ui_events->emit_terminal("terminal.pty_output", std::move(pre_paint), __FILE__,
                                    __LINE__);
}

}  // namespace tgdb
