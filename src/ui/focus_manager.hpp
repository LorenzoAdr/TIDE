#pragma once

namespace tgdb {

enum class FocusRegion { Explorer, Editor, RightPanel, Terminal };

struct FocusManagerState {
  FocusRegion region = FocusRegion::Editor;

  void cycle_forward();
  void cycle_backward();
  void move_left();
  void move_right();
  void move_down();
  void move_up();
  const char* region_label() const;
};

}  // namespace tgdb
