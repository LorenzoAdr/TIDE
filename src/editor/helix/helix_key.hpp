#pragma once

#include <optional>
#include <string>

#include "ftxui/component/event.hpp"

namespace tuide {

// Serialized key token used in the keymap trie (Helix-style).
std::optional<std::string> helix_key_token(const ftxui::Event& event);

bool helix_event_is_printable(const ftxui::Event& event, char* out_char);

}  // namespace tuide
