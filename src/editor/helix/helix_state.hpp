#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/text_search.hpp"

namespace tgdb {

enum class HelixMode {
  kNormal,
  kInsert,
  kSelect,
};

enum class HelixRegexPromptKind {
  kNone,
  kSelect,
  kSplit,
};

enum class HelixCharFindKind {
  kNone,
  kFind,
  kTill,
  kFindBack,
  kTillBack,
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
  HelixRegexPromptKind regex_prompt = HelixRegexPromptKind::kNone;
  std::string regex_prompt_buffer;
  TextRange regex_scope{};
  bool regex_scope_valid = false;
  HelixCharFindKind char_find_pending = HelixCharFindKind::kNone;
  HelixCharFindKind char_find_last = HelixCharFindKind::kNone;
  char char_find_char = '\0';

  void clear_pending();
  void clear_count();
  void clear_command();
  void clear_regex_prompt();
  void clear_char_find_pending();
  bool prefix_active() const { return !pending_keys.empty(); }
  bool prompt_active() const { return command_mode || regex_prompt != HelixRegexPromptKind::kNone; }
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
