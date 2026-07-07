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

void HelixEditorState::clear_char_find_pending() {
  char_find_pending = HelixCharFindKind::kNone;
}

namespace {

const char* char_find_pending_label(HelixCharFindKind kind) {
  switch (kind) {
    case HelixCharFindKind::kFind:
      return "f";
    case HelixCharFindKind::kTill:
      return "t";
    case HelixCharFindKind::kFindBack:
      return "F";
    case HelixCharFindKind::kTillBack:
      return "T";
    case HelixCharFindKind::kNone:
    default:
      return "";
  }
}

}  // namespace

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
  if (char_find_pending != HelixCharFindKind::kNone) {
    if (out.tellp() > 0) {
      out << ' ';
    }
    out << char_find_pending_label(char_find_pending);
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
  helix->clear_char_find_pending();
  helix->help_open = false;
}

}  // namespace tgdb
