#include "ui/focus_manager.hpp"

#include "i18n/tr.hpp"

namespace tgdb {

bool FocusManagerState::secondary_visible() const {
  return secondary_editor_visible && secondary_editor_visible();
}

void FocusManagerState::cycle_forward() {
  switch (region) {
    case FocusRegion::Explorer:
      region = FocusRegion::Editor;
      break;
    case FocusRegion::Editor:
      region = secondary_visible() ? FocusRegion::SecondaryEditor : FocusRegion::RightPanel;
      break;
    case FocusRegion::SecondaryEditor:
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
    case FocusRegion::SecondaryEditor:
      region = FocusRegion::Editor;
      break;
    case FocusRegion::RightPanel:
      region = secondary_visible() ? FocusRegion::SecondaryEditor : FocusRegion::Editor;
      break;
    case FocusRegion::Terminal:
      region = FocusRegion::RightPanel;
      break;
  }
}

void FocusManagerState::move_left() {
  if (region == FocusRegion::Editor) {
    region = FocusRegion::Explorer;
  } else if (region == FocusRegion::SecondaryEditor) {
    region = FocusRegion::Editor;
  } else if (region == FocusRegion::RightPanel) {
    region = secondary_visible() ? FocusRegion::SecondaryEditor : FocusRegion::Editor;
  }
}

void FocusManagerState::move_right() {
  if (region == FocusRegion::Editor) {
    region = secondary_visible() ? FocusRegion::SecondaryEditor : FocusRegion::RightPanel;
  } else if (region == FocusRegion::SecondaryEditor) {
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

std::string FocusManagerState::region_label() const {
  switch (region) {
    case FocusRegion::Explorer:
      return i18n::tr("focus.region.explorer");
    case FocusRegion::Editor:
      return i18n::tr("focus.region.editor");
    case FocusRegion::SecondaryEditor:
      return i18n::tr("focus.region.editor_secondary");
    case FocusRegion::RightPanel:
      return i18n::tr("focus.region.outline");
    case FocusRegion::Terminal:
      return i18n::tr("focus.region.terminal");
  }
  return {};
}

}  // namespace tgdb
