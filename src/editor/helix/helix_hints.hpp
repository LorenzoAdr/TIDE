#pragma once

#include <string>
#include <vector>

#include "editor/helix/helix_keymap.hpp"
#include "editor/helix/helix_state.hpp"
#include "ftxui/dom/elements.hpp"

namespace tgdb {

ftxui::Element make_helix_hint_overlay(const HelixEditorState& helix);

ftxui::Element make_helix_help_overlay(const HelixEditorState& helix);

ftxui::Element make_helix_command_overlay(const HelixEditorState& helix);

std::vector<std::pair<std::string, std::string>> helix_help_sections();

}  // namespace tgdb
