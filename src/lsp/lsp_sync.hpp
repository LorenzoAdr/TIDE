#pragma once

#include <cstdint>

namespace tgdb {

// These used to be a single shared 1000ms constant. Splitting them (and staggering the
// values) avoids the didChange flush, the diagnostics reveal, ui_activity_gate's deferred
// editor sync, and the semantic-token retry timer all landing in the same ~50ms window
// after the user stops typing, which used to show up as a single concentrated CPU/render
// spike about a second after the last keystroke instead of several smaller, spread-out
// ones. See also kDeferredEditorSyncIdleMs (util/ui_activity_gate.cpp) and
// kSemanticTokenRetryIntervalMs (lsp/lsp_client.cpp).

// Debounce for sending didChange to clangd after the last edit.
constexpr int64_t kLspDidChangeDebounceMs = 900;

// Debounce for revealing diagnostics in the UI after the last edit (kept separate from
// the didChange debounce above so the diagnostics "reveal" doesn't land in the exact
// same tick as the didChange flush + its own follow-up work).
constexpr int64_t kLspDiagnosticsDisplayDebounceMs = 1150;

// When true, LSP semantic token fetches are skipped (tree-sitter only). Code paths stay
// in place so the impact can be measured by toggling this flag.
constexpr bool kLspSemanticTokensStandby = true;

// Debounce before evaluating whether a live completion LSP fetch is needed (scope jump,
// trigger character, etc.).
constexpr int kLiveCompletionTriggerDebounceMs = 50;

}  // namespace tgdb
