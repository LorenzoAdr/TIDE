#include "ui/keybind/key_action.hpp"

#include <array>
#include <string_view>

namespace tuide {
namespace {

struct ActionMeta {
  KeyAction action;
  std::string_view id;
  std::string_view label_key;
};

constexpr std::array<ActionMeta, static_cast<std::size_t>(KeyAction::Count)> kActionMeta{{
    {KeyAction::OpenExternalFile, "open_external_file", "shortcuts.general.open_external"},
    {KeyAction::OpenExternalFileHere, "open_external_file_here",
     "shortcuts.general.open_external_here"},
    {KeyAction::OpenShortcutsModal, "open_shortcuts_modal",
     "shortcuts.general.keyboard_shortcuts"},
    {KeyAction::OpenDebugWizard, "open_debug_wizard", "shortcuts.general.debug_wizard"},
    {KeyAction::OpenWorkspaceWizard, "open_workspace_wizard", "shortcuts.general.change_workspace"},
    {KeyAction::FocusTerminalTab, "focus_terminal_tab", "shortcuts.general.terminal_tab"},
    {KeyAction::QuickLaunch, "quick_launch", "shortcuts.general.quick_launch"},
    {KeyAction::OpenSearchPanel, "open_search_panel", "shortcuts.general.search_panel"},
    {KeyAction::OpenOutlinePanel, "open_outline_panel", "shortcuts.general.outline_panel"},
    {KeyAction::OpenProblemsPanel, "open_problems_panel", "shortcuts.general.problems_panel"},
    {KeyAction::OpenBinarySymbolsPanel, "open_binary_symbols_panel",
     "shortcuts.general.binary_symbols"},
    {KeyAction::OpenSettings, "open_settings", "shortcuts.general.settings"},
    {KeyAction::QuickOpen, "quick_open", "shortcuts.general.quick_open"},
    {KeyAction::GoToSymbol, "go_to_symbol", "shortcuts.general.go_to_symbol"},
    {KeyAction::ToggleBottomPanel, "toggle_bottom_panel", "shortcuts.general.toggle_bottom_panel"},
    {KeyAction::ToggleConsoleExpand, "toggle_console_expand",
     "shortcuts.general.toggle_console_expand"},
    {KeyAction::Quit, "quit", "shortcuts.general.quit"},
    {KeyAction::FocusExplorer, "focus_explorer", "shortcuts.general.focus_explorer"},
    {KeyAction::FocusEditor, "focus_editor", "shortcuts.general.focus_editor"},
    {KeyAction::FocusMoveLeft, "focus_move_left", "shortcuts.general.move_focus_horizontal"},
    {KeyAction::FocusMoveRight, "focus_move_right", "shortcuts.general.move_focus_horizontal"},
    {KeyAction::FocusMoveDown, "focus_move_down", "shortcuts.general.move_focus_vertical"},
    {KeyAction::FocusMoveUp, "focus_move_up", "shortcuts.general.move_focus_vertical"},
    {KeyAction::WorkspaceSearchSelection, "workspace_search_selection",
     "shortcuts.editor.find_in_workspace"},
    {KeyAction::ToggleHelixMode, "toggle_helix_mode", "shortcuts.general.helix_toggle"},

    {KeyAction::SaveFile, "save_file", "shortcuts.editor.save"},
    {KeyAction::FindInFile, "find_in_file", "shortcuts.editor.find_in_file"},
    {KeyAction::GoToLine, "go_to_line", "shortcuts.editor.goto_line"},
    {KeyAction::Undo, "undo", "shortcuts.editor.undo"},
    {KeyAction::Redo, "redo", "shortcuts.editor.redo"},
    {KeyAction::Copy, "copy", "shortcuts.editor.copy"},
    {KeyAction::Cut, "cut", "shortcuts.editor.cut"},
    {KeyAction::Paste, "paste", "shortcuts.editor.paste"},
    {KeyAction::CommentLines, "comment_lines", "shortcuts.editor.comment_lines"},
    {KeyAction::UncommentLines, "uncomment_lines", "shortcuts.editor.uncomment_lines"},
    {KeyAction::HalfPageUp, "half_page_up", "shortcuts.editor.half_page_up"},
    {KeyAction::HalfPageDown, "half_page_down", "shortcuts.editor.half_page_down"},
    {KeyAction::DeleteWordBackward, "delete_word_backward",
     "shortcuts.editor.delete_word_backward"},
    {KeyAction::DeleteWordForward, "delete_word_forward", "shortcuts.editor.delete_word_forward"},
    {KeyAction::SelectNextMatch, "select_next_match", "shortcuts.editor.select_next_match"},
    {KeyAction::SelectAllMatches, "select_all_matches", "shortcuts.editor.select_all_matches"},
    {KeyAction::TriggerCompletion, "trigger_completion", "shortcuts.editor.complete_lsp"},
    {KeyAction::GoToDefinition, "go_to_definition", "shortcuts.editor.go_to_definition"},
    {KeyAction::GoToDeclaration, "go_to_declaration", "shortcuts.editor.go_to_declaration"},
    {KeyAction::CursorHistoryBack, "cursor_history_back", "shortcuts.editor.cursor_history"},
    {KeyAction::CursorHistoryForward, "cursor_history_forward", "shortcuts.editor.cursor_history"},
    {KeyAction::Indent, "indent", "shortcuts.editor.indent"},
    {KeyAction::Unindent, "unindent", "shortcuts.editor.indent"},
    {KeyAction::ToggleBreakpoint, "toggle_breakpoint", "shortcuts.debug.toggle_breakpoint"},
    {KeyAction::ExtendLeft, "extend_left", "shortcuts.editor.extend_selection"},
    {KeyAction::ExtendRight, "extend_right", "shortcuts.editor.extend_selection"},
    {KeyAction::ExtendUp, "extend_up", "shortcuts.editor.extend_selection"},
    {KeyAction::ExtendDown, "extend_down", "shortcuts.editor.extend_selection"},
    {KeyAction::ExtendHome, "extend_home", "shortcuts.editor.extend_selection"},
    {KeyAction::ExtendEnd, "extend_end", "shortcuts.editor.extend_selection"},
    {KeyAction::WordLeft, "word_left", "shortcuts.editor.move_by_words"},
    {KeyAction::WordRight, "word_right", "shortcuts.editor.move_by_words"},
    {KeyAction::BlockSelectUp, "block_select_up", "shortcuts.editor.block_selection"},
    {KeyAction::BlockSelectDown, "block_select_down", "shortcuts.editor.block_selection"},

    {KeyAction::DebugContinue, "debug_continue", "shortcuts.debug.continue"},
    {KeyAction::DebugStepOver, "debug_step_over", "shortcuts.debug.step_over"},
    {KeyAction::DebugStepInto, "debug_step_into", "shortcuts.debug.step_into"},
    {KeyAction::DebugStepOut, "debug_step_out", "shortcuts.debug.step_out"},
    {KeyAction::DebugSourceSubstitute, "debug_source_substitute",
     "shortcuts.debug.source_substitute"},
}};

}  // namespace

std::string_view key_action_id(KeyAction action) {
  const auto idx = static_cast<std::size_t>(action);
  if (idx >= kActionMeta.size()) {
    return {};
  }
  return kActionMeta[idx].id;
}

std::string_view key_action_label_key(KeyAction action) {
  const auto idx = static_cast<std::size_t>(action);
  if (idx >= kActionMeta.size()) {
    return {};
  }
  return kActionMeta[idx].label_key;
}

KeyAction key_action_from_id(std::string_view id) {
  for (const auto& meta : kActionMeta) {
    if (meta.id == id) {
      return meta.action;
    }
  }
  return KeyAction::Count;
}

}  // namespace tuide
