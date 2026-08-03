#pragma once

#include <cstdint>
#include <string_view>

namespace tuide {

// Stable IDs for non-Helix shortcuts. Helix trie bindings stay outside this enum.
enum class KeyAction : uint16_t {
  // Global / app
  OpenExternalFile,
  OpenExternalFileHere,
  OpenShortcutsModal,
  OpenDebugWizard,
  OpenWorkspaceWizard,
  FocusTerminalTab,
  QuickLaunch,
  OpenSearchPanel,
  OpenOutlinePanel,
  OpenProblemsPanel,
  OpenBinarySymbolsPanel,
  OpenSettings,
  QuickOpen,
  GoToSymbol,
  ToggleBottomPanel,
  Quit,
  FocusExplorer,
  FocusEditor,
  FocusMoveLeft,
  FocusMoveRight,
  FocusMoveDown,
  FocusMoveUp,
  WorkspaceSearchSelection,
  ToggleHelixMode,  // reserved; not remappable in phase 2 UI

  // Editor
  SaveFile,
  FindInFile,
  GoToLine,
  Undo,
  Redo,
  Copy,
  Cut,
  Paste,
  CommentLines,
  UncommentLines,
  HalfPageUp,
  HalfPageDown,
  DeleteWordBackward,
  DeleteWordForward,
  SelectNextMatch,
  SelectAllMatches,
  TriggerCompletion,
  GoToDefinition,
  GoToDeclaration,
  CursorHistoryBack,
  CursorHistoryForward,
  Indent,
  Unindent,
  ToggleBreakpoint,
  ExtendLeft,
  ExtendRight,
  ExtendUp,
  ExtendDown,
  ExtendHome,
  ExtendEnd,
  WordLeft,
  WordRight,
  BlockSelectUp,
  BlockSelectDown,

  // Debug
  DebugContinue,
  DebugStepOver,
  DebugStepInto,
  DebugStepOut,
  DebugSourceSubstitute,

  Count
};

enum class KeyScope : uint8_t {
  Global = 1,
  Editor = 2,
  Debug = 4,
  Any = Global | Editor | Debug,
};

inline constexpr uint8_t to_mask(KeyScope scope) {
  return static_cast<uint8_t>(scope);
}

std::string_view key_action_id(KeyAction action);
std::string_view key_action_label_key(KeyAction action);
KeyAction key_action_from_id(std::string_view id);

}  // namespace tuide
