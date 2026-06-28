#pragma once

#include <string>
#include <vector>

#include "editor/bracket_match.hpp"
#include "editor/editor_state.hpp"
#include "editor/text_search.hpp"
#include "ftxui/dom/elements.hpp"
#include "lsp/diagnostics.hpp"
#include "lsp/semantic_tokens.hpp"

namespace tgdb {

struct EditorDecoration {
  int start_col = 0;
  int end_col = 0;
  enum class Kind {
    FindMatch,
    DiagnosticWarning,
    DiagnosticError,
    MatchingBracket,
    Selection,
    PrimaryCaret,
    SecondaryCaret,
  } kind = Kind::Selection;
};

void collect_find_decorations(int line_index, const std::vector<TextMatch>& matches,
                              std::vector<EditorDecoration>* out);

void collect_line_decorations(int line_index, const EditorBuffer& buffer, bool editor_focused,
                              std::vector<EditorDecoration>* out);

void collect_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                 std::vector<EditorDecoration>* out);

void collect_diagnostic_decorations(int line_index, const std::vector<Diagnostic>& diagnostics,
                                    std::vector<EditorDecoration>* out);

ftxui::Element RenderEditorLine(const std::string& line, int line_index,
                               const EditorBuffer& buffer, bool editor_focused,
                               const std::vector<TextMatch>* find_matches = nullptr,
                               const SemanticTokenDocument* semantic_tokens = nullptr,
                               const BracketPairHighlight* bracket = nullptr,
                               const std::vector<Diagnostic>* line_diagnostics = nullptr,
                               const std::string* diagnostic_suffix = nullptr);

}  // namespace tgdb
