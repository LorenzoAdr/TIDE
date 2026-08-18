#include <cassert>
#include <cstdint>
#include <iostream>

#include "ui/ui_invalidation_policy.hpp"
#include "util/ui_panel_render_cache.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_mask(tuide::UiInvalidation kind, std::uint8_t expected, const char* message) {
  const std::uint8_t actual = tuide::ui_invalidation_panel_mask(kind);
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got 0x" << std::hex << int(actual) << " expected 0x"
              << int(expected) << std::dec << ")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  using tuide::UiInvalidation;
  using tuide::kPanelChromeAll;
  using tuide::kPanelConsoleOnly;
  using tuide::kPanelEditorOnly;
  using tuide::kPanelEditorOutline;
  using tuide::kPanelFileTreeOnly;
  using tuide::kPanelLspDiagnostics;
  using tuide::kPanelOpenOrJump;

  expect_mask(UiInvalidation::OpenFile, kPanelOpenOrJump, "OpenFile");
  expect_mask(UiInvalidation::JumpSameFile, kPanelEditorOnly, "JumpSameFile");
  expect_mask(UiInvalidation::JumpCrossFile, kPanelOpenOrJump, "JumpCrossFile");
  expect_mask(UiInvalidation::TreeSitterActiveFile, kPanelEditorOutline, "TreeSitterActiveFile");
  expect_mask(UiInvalidation::TreeSitterInactiveFile, 0, "TreeSitterInactiveFile");
  expect_mask(UiInvalidation::LspCompletion, kPanelEditorOnly, "LspCompletion");
  expect_mask(UiInvalidation::LspHover, kPanelEditorOnly, "LspHover");
  expect_mask(UiInvalidation::LspDiagnostics, kPanelLspDiagnostics, "LspDiagnostics");
  expect_mask(UiInvalidation::LspDocumentSymbols, 0, "LspDocumentSymbols no wake panels");
  expect_mask(UiInvalidation::VisualHighlight, kPanelEditorOnly, "VisualHighlight");
  expect_mask(UiInvalidation::FindMatches, kPanelEditorOnly, "FindMatches");
  expect_mask(UiInvalidation::TerminalOutput, kPanelConsoleOnly, "TerminalOutput");
  expect_mask(UiInvalidation::FileTreeStructure, kPanelFileTreeOnly, "FileTreeStructure");
  expect_mask(UiInvalidation::IndexerFsChange, kPanelFileTreeOnly, "IndexerFsChange");
  expect_mask(UiInvalidation::ThemeOrResize, kPanelChromeAll, "ThemeOrResize");
  expect_mask(UiInvalidation::StatusChrome, 0, "StatusChrome no panel mask");
  expect_mask(UiInvalidation::EditorViewOnly, kPanelEditorOnly, "EditorViewOnly");

  expect(!tuide::ui_invalidation_spec(UiInvalidation::TreeSitterInactiveFile).wake,
         "inactive TS must not wake");
  expect(tuide::ui_invalidation_spec(UiInvalidation::OpenFile).wake, "OpenFile wakes");
  expect(tuide::ui_invalidation_spec(UiInvalidation::OpenFile).focus_sync, "OpenFile focus sync");
  expect(!tuide::ui_invalidation_spec(UiInvalidation::EditorViewOnly).wake,
         "EditorViewOnly must not wake (avoids tick→invalidate storm)");

  // Cache: dirty generation advances per panel independently.
  tuide::UiPanelRenderCache cache;
  cache.mark_dirty(tuide::UiPanelId::EditorCenter);
  expect(cache.entries[1].dirty_generation == 2, "editor dirty gen");
  expect(cache.entries[0].dirty_generation == 1, "file tree untouched");

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "ui_invalidation_policy_test ok\n";
  return 0;
}
