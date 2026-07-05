#include "editor/helix/helix_state.hpp"

#include <sstream>

#include "i18n/tr.hpp"

namespace tgdb {

void HelixEditorState::clear_pending() {
  pending_keys.clear();
  hint_visible = false;
}

void HelixEditorState::clear_count() {
  count = 0;
}

void HelixEditorState::clear_command() {
  command_mode = false;
  command_buffer.clear();
}

std::string HelixEditorState::pending_label() const {
  std::ostringstream out;
  if (count > 0) {
    out << count;
  }
  for (std::size_t i = 0; i < pending_keys.size(); ++i) {
    if (out.tellp() > 0) {
      out << ' ';
    }
    out << pending_keys[i];
  }
  if (command_mode) {
    if (out.tellp() > 0) {
      out << ' ';
    }
    out << ':' << command_buffer;
  }
  return out.str();
}

std::string HelixEditorState::mode_label() const {
  if (command_mode) {
    return i18n::tr("helix.mode.command");
  }
  switch (mode) {
    case HelixMode::kNormal:
      return i18n::tr("helix.mode.normal");
    case HelixMode::kInsert:
      return i18n::tr("helix.mode.insert");
    case HelixMode::kSelect:
      return i18n::tr("helix.mode.select");
  }
  return i18n::tr("helix.mode.normal");
}

void reset_helix_editor_state(HelixEditorState* helix) {
  if (helix == nullptr) {
    return;
  }
  helix->mode = HelixMode::kNormal;
  helix->clear_pending();
  helix->clear_count();
  helix->clear_command();
  helix->help_open = false;
}

}  // namespace tgdb
