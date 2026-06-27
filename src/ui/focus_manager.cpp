#include "ui/focus_manager.hpp"

namespace tgdb {

void FocusManagerState::cycle_forward() {
  switch (region) {
    case FocusRegion::Explorer:
      region = FocusRegion::Editor;
      break;
    case FocusRegion::Editor:
      region = FocusRegion::RightPanel;
      break;
    case FocusRegion::RightPanel:
      region = FocusRegion::Terminal;
      break;
    case FocusRegion::Terminal:
      region = FocusRegion::Explorer;
      break;
  }
}

void FocusManagerState::cycle_backward() {
  switch (region) {
    case FocusRegion::Explorer:
      region = FocusRegion::Terminal;
      break;
    case FocusRegion::Editor:
      region = FocusRegion::Explorer;
      break;
    case FocusRegion::RightPanel:
      region = FocusRegion::Editor;
      break;
    case FocusRegion::Terminal:
      region = FocusRegion::RightPanel;
      break;
  }
}

void FocusManagerState::move_left() {
  if (region == FocusRegion::Editor) {
    region = FocusRegion::Explorer;
  } else if (region == FocusRegion::RightPanel) {
    region = FocusRegion::Editor;
  }
}

void FocusManagerState::move_right() {
  if (region == FocusRegion::Editor) {
    region = FocusRegion::RightPanel;
  } else if (region == FocusRegion::Explorer) {
    region = FocusRegion::Editor;
  }
}

void FocusManagerState::move_down() {
  region = FocusRegion::Terminal;
}

void FocusManagerState::move_up() {
  if (region == FocusRegion::Terminal) {
    region = FocusRegion::Editor;
  }
}

const char* FocusManagerState::region_label() const {
  switch (region) {
    case FocusRegion::Explorer:
      return "Explorador";
    case FocusRegion::Editor:
      return "Editor";
    case FocusRegion::RightPanel:
      return "Outline";
    case FocusRegion::Terminal:
      return "Terminal";
  }
  return "";
}

}  // namespace tgdb
