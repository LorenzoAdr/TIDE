#pragma once

#include <functional>

namespace tgdb {

enum class FocusRegion { Explorer, Editor, SecondaryEditor, RightPanel, Terminal };

inline bool is_editor_focus_region(FocusRegion region) {
  return region == FocusRegion::Editor || region == FocusRegion::SecondaryEditor;
}

struct FocusManagerState {
  FocusRegion region = FocusRegion::Editor;
  std::function<bool()> secondary_editor_visible;

  void cycle_forward();
  void cycle_backward();
  void move_left();
  void move_right();
  void move_down();
  void move_up();
  const char* region_label() const;

 private:
  bool secondary_visible() const;
};

}  // namespace tgdb
