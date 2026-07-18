#pragma once

// Política centralizada: qué eventos asíncronos deben forzar un redibujado de la UI.
//
// Cada entrada documenta una fuente async, su etiqueta de traza y el UiEventKind que
// determina el plan de drenaje (run_editor, run_debug, run_terminal, …).
//
// Fuentes incluidas en la política del producto:
//   1. LspCompletion        — autocompletado clangd al escribir (respuesta async)
//   2. TreeSitterReady      — parse tree-sitter (color global + outline)
//   3. LspDiagnostics       — publishDiagnostics / errores de compilación
//   4. LspHover             — información de símbolo clangd (p. ej. menú «Información»)
//   5. EditorNavigation     — ir a definición / implementación (navegación diferida)
//   6. DebugCritical        — parada, reanudación, sesión terminada, sesión lista
//   7. TerminalOutput       — salida PTY línea a línea
//
// Fuentes secundarias (wake explícito, mismo mecanismo):
//   - LspDocumentSymbols    — símbolos LSP para outline (tree-sitter es la vía principal)
//   - LspSemanticTokens     — resaltado LSP (standby; tree-sitter cubre el color)
//   - FindMatchesUpdated    — búsqueda incremental async en el editor
//   - FilePickerPreview     — vista previa async del selector de archivos
//   - VisualHighlightSync   — realce visual del editor (debounce 200ms, hilo vh-compute)
//
// NO despiertan por diseño (se drenan cuando otro wake corre):
//   - GitIndexerUpdated     — panel git; callback vacío
//   - WorkspaceIndexUpdated — cambios del indexador de workspace
//   - DebugOutput           — consola GDB entre paradas (batch al parar)
//   - DebugStackVariables   — stack/variables (batch con parada)

#include <string_view>

#include "ui/main_layout.hpp"
#include "ui/ui_event_types.hpp"
#include "ui/ui_wake.hpp"

namespace tuide {

enum class UiWakeReason {
  LspCompletion,
  LspHover,
  LspDiagnostics,
  LspDocumentSymbols,
  LspSemanticTokens,
  TreeSitterReady,
  EditorNavigationScheduled,
  EditorNavigationComplete,
  DebugStopped,
  DebugContinued,
  DebugTerminated,
  DebugSessionReady,
  TerminalOutput,
  FindMatchesUpdated,
  FilePickerPreview,
  VisualHighlightSync,
};

struct UiWakeSpec {
  std::string_view tag;
  UiEventKind kind;
};

inline UiWakeSpec ui_wake_spec(UiWakeReason reason) {
  switch (reason) {
    case UiWakeReason::LspCompletion:
      return {"lsp.completion", UiEventKind::InputCorrelated};
    case UiWakeReason::LspHover:
      return {"lsp.hover", UiEventKind::InputCorrelated};
    case UiWakeReason::LspDiagnostics:
      return {"lsp.diagnostics", UiEventKind::InputCorrelated};
    case UiWakeReason::LspDocumentSymbols:
      return {"lsp.document_symbols", UiEventKind::InputCorrelated};
    case UiWakeReason::LspSemanticTokens:
      return {"lsp.semantic_tokens", UiEventKind::InputCorrelated};
    case UiWakeReason::TreeSitterReady:
      return {"tree_sitter.ready", UiEventKind::InputCorrelated};
    case UiWakeReason::EditorNavigationScheduled:
      return {"editor.navigation.scheduled", UiEventKind::InputCorrelated};
    case UiWakeReason::EditorNavigationComplete:
      return {"editor.navigation.complete", UiEventKind::InputCorrelated};
    case UiWakeReason::DebugStopped:
      return {"debug.stopped", UiEventKind::DebugCritical};
    case UiWakeReason::DebugContinued:
      return {"debug.continued", UiEventKind::DebugCritical};
    case UiWakeReason::DebugTerminated:
      return {"debug.terminated", UiEventKind::DebugCritical};
    case UiWakeReason::DebugSessionReady:
      return {"debug.session_ready", UiEventKind::DebugCritical};
    case UiWakeReason::TerminalOutput:
      return {"terminal.pty_output", UiEventKind::TerminalOutput};
    case UiWakeReason::FindMatchesUpdated:
      return {"editor.find_matches", UiEventKind::InputCorrelated};
    case UiWakeReason::FilePickerPreview:
      return {"file_picker.preview", UiEventKind::InputCorrelated};
    case UiWakeReason::VisualHighlightSync:
      return {"editor.visual_highlight", UiEventKind::InputCorrelated};
  }
  return {"wake", UiEventKind::InputCorrelated};
}

inline void ui_wake_reason(MainLayoutState* layout, UiWakeReason reason) {
  const UiWakeSpec spec = ui_wake_spec(reason);
  ui_wake(layout, spec.tag, spec.kind);
}

#define UI_WAKE_REASON(layout, reason) ui_wake_reason((layout), (reason))

}  // namespace tuide
