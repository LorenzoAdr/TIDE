#pragma once

// Política declarativa: qué paneles FTXUI marcar dirty y despertar.
// El busy strip ANSI NO entra aquí (ver busy_strip.hpp).

#include <cstdint>
#include <string_view>

#include "ui/ui_event_types.hpp"
#include "util/ui_panel_render_cache.hpp"

namespace tuide {

struct MainLayoutState;

enum class UiInvalidation : std::uint16_t {
  OpenFile,
  JumpSameFile,
  JumpCrossFile,
  TreeSitterActiveFile,
  TreeSitterInactiveFile,
  LspCompletion,
  LspHover,
  LspDiagnostics,
  LspDocumentSymbols,
  LspSemanticTokens,
  VisualHighlight,
  FindMatches,
  StatusChrome,
  TerminalOutput,
  DebugStopped,
  DebugContinued,
  DebugSessionReady,
  FileTreeStructure,
  IndexerFsChange,
  ThemeOrResize,
  AppModeChanged,
  LayoutChromeChanged,
  EditorViewOnly,
};

struct UiInvalidationSpec {
  std::uint8_t panel_mask = 0;
  bool wake = true;
  bool focus_sync = false;
  UiEventKind kind = UiEventKind::InputCorrelated;
  std::string_view default_tag;
};

constexpr std::uint8_t panel_bit(UiPanelId id) {
  return static_cast<std::uint8_t>(1u << static_cast<unsigned>(id));
}

constexpr std::uint8_t kPanelEditorOnly = panel_bit(UiPanelId::EditorCenter);
constexpr std::uint8_t kPanelEditorOutline =
    panel_bit(UiPanelId::EditorCenter) | panel_bit(UiPanelId::RightSidebar);
constexpr std::uint8_t kPanelOpenOrJump = panel_bit(UiPanelId::EditorCenter) |
                                          panel_bit(UiPanelId::RightSidebar) |
                                          panel_bit(UiPanelId::FileTree);
constexpr std::uint8_t kPanelDebugStop =
    panel_bit(UiPanelId::EditorCenter) | panel_bit(UiPanelId::RightSidebar);
constexpr std::uint8_t kPanelChromeAll =
    panel_bit(UiPanelId::FileTree) | panel_bit(UiPanelId::EditorCenter) |
    panel_bit(UiPanelId::RightSidebar) | panel_bit(UiPanelId::Console);
constexpr std::uint8_t kPanelFileTreeOnly = panel_bit(UiPanelId::FileTree);
constexpr std::uint8_t kPanelConsoleOnly = panel_bit(UiPanelId::Console);
constexpr std::uint8_t kPanelRightSidebarOnly = panel_bit(UiPanelId::RightSidebar);

inline UiInvalidationSpec ui_invalidation_spec(UiInvalidation kind) {
  switch (kind) {
    case UiInvalidation::OpenFile:
      return {kPanelOpenOrJump, true, true, UiEventKind::InputCorrelated, "editor.open"};
    case UiInvalidation::JumpSameFile:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated,
              "editor.navigation.same_file"};
    case UiInvalidation::JumpCrossFile:
      return {kPanelOpenOrJump, true, true, UiEventKind::InputCorrelated,
              "editor.navigation.cross_file"};
    case UiInvalidation::TreeSitterActiveFile:
      return {kPanelEditorOutline, true, false, UiEventKind::InputCorrelated, "tree_sitter.ready"};
    case UiInvalidation::TreeSitterInactiveFile:
      return {0, false, false, UiEventKind::InputCorrelated, "tree_sitter.inactive"};
    case UiInvalidation::LspCompletion:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated, "lsp.completion"};
    case UiInvalidation::LspHover:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated, "lsp.hover"};
    case UiInvalidation::LspDiagnostics:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated, "lsp.diagnostics"};
    case UiInvalidation::LspDocumentSymbols:
      return {0, false, false, UiEventKind::InputCorrelated, "lsp.document_symbols"};
    case UiInvalidation::LspSemanticTokens:
      return {0, false, false, UiEventKind::InputCorrelated, "lsp.semantic_tokens"};
    case UiInvalidation::VisualHighlight:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated,
              "editor.visual_highlight"};
    case UiInvalidation::FindMatches:
      return {kPanelEditorOnly, true, true, UiEventKind::InputCorrelated, "editor.find_matches"};
    case UiInvalidation::StatusChrome:
      return {0, true, false, UiEventKind::InputCorrelated, "status.chrome"};
    case UiInvalidation::TerminalOutput:
      return {kPanelConsoleOnly, true, false, UiEventKind::TerminalOutput, "terminal.pty_output"};
    case UiInvalidation::DebugStopped:
      return {kPanelDebugStop, true, true, UiEventKind::DebugCritical, "debug.stopped"};
    case UiInvalidation::DebugContinued:
      return {kPanelDebugStop, true, true, UiEventKind::DebugCritical, "debug.continued"};
    case UiInvalidation::DebugSessionReady:
      return {kPanelDebugStop, true, true, UiEventKind::DebugCritical, "debug.session_ready"};
    case UiInvalidation::FileTreeStructure:
      return {kPanelFileTreeOnly, true, false, UiEventKind::InputCorrelated, "file_tree.structure"};
    case UiInvalidation::IndexerFsChange:
      return {kPanelFileTreeOnly, true, false, UiEventKind::InputCorrelated, "indexer.fs_change"};
    case UiInvalidation::ThemeOrResize:
      return {kPanelChromeAll, true, false, UiEventKind::InputCorrelated, "ui.theme_or_resize"};
    case UiInvalidation::AppModeChanged:
      return {kPanelChromeAll, true, true, UiEventKind::InputCorrelated, "app.mode_changed"};
    case UiInvalidation::LayoutChromeChanged:
      return {kPanelChromeAll, true, false, UiEventKind::InputCorrelated, "layout.chrome"};
    case UiInvalidation::EditorViewOnly:
      // Dirty only: input/tick paths already redraw. Async callers must UI_WAKE explicitly
      // (see visual_highlight debounce/result wake callbacks). wake=true caused a Custom
      // storm via editor tick → invalidate_editor_view → wake → tick…
      return {kPanelEditorOnly, false, true, UiEventKind::InputCorrelated, "editor.view"};
  }
  return {0, false, false, UiEventKind::InputCorrelated, "invalidation.unknown"};
}

inline std::uint8_t ui_invalidation_panel_mask(UiInvalidation kind) {
  return ui_invalidation_spec(kind).panel_mask;
}

void invalidate(MainLayoutState* layout, UiInvalidation kind, std::string_view tag = {});

}  // namespace tuide
