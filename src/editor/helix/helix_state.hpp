#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tgdb {

enum class HelixMode {
  kNormal,
  kInsert,
  kSelect,
};

struct HelixEditorState {
  HelixMode mode = HelixMode::kNormal;
  std::vector<std::string> pending_keys;
  std::string yank_register;
  bool hint_visible = false;
  bool help_open = false;
  int count = 0;
  bool command_mode = false;
  std::string command_buffer;

  void clear_pending();
  void clear_count();
  void clear_command();
  bool prefix_active() const { return !pending_keys.empty(); }
  std::string pending_label() const;
  std::string mode_label() const;
};

void reset_helix_editor_state(HelixEditorState* helix);

struct HelixStatusSnapshot {
  bool active = false;
  std::string mode;
  std::string pending;
};

}  // namespace tgdb
