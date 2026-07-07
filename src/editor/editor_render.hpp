#pragma once

#include <string>
#include <vector>

#include "editor/bracket_match.hpp"
#include "editor/editor_state.hpp"
#include "editor/indent_guides.hpp"
#include "editor/text_search.hpp"
#include "ftxui/dom/elements.hpp"
#include "lsp/diagnostics.hpp"
#include "lsp/semantic_tokens.hpp"
#include "ui/theme.hpp"
#include "util/syntax_highlight.hpp"

namespace tgdb {

struct EditorDecoration {
  int start_col = 0;
  int end_col = 0;
  int brace_depth = 0;
  enum class Kind {
    FindMatch,
    SelectionOccurrence,
    DiagnosticWarning,
    DiagnosticError,
    MatchingBracket,
    ScopeBrace,
    ColoredBrace,
    Selection,
    PrimaryCaret,
    SecondaryCaret,
    PressFlash,
  } kind = Kind::Selection;
};

struct EditorSymbolPress {
  int start_col = 0;
  int end_col = 0;
  bool active = false;
};

void collect_press_decorations(int line_index, const EditorSymbolPress& press,
                               std::vector<EditorDecoration>* out);

void collect_find_decorations(int line_index, const std::vector<TextMatch>& matches,
                              std::vector<EditorDecoration>* out);

void collect_selection_occurrence_decorations(int line_index,
                                              const std::vector<TextMatch>& matches,
                                              std::vector<EditorDecoration>* out);

void collect_line_decorations(int line_index, const EditorBuffer& buffer, bool editor_focused,
                              bool show_caret, std::vector<EditorDecoration>* out);

void collect_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                 std::vector<EditorDecoration>* out);

void collect_scope_bracket_decorations(int line_index, const BracketPairHighlight& bracket,
                                       int scope_highlight_strength,
                                       std::vector<EditorDecoration>* out);

void collect_colored_brace_decorations(int line_index,
                                       const std::vector<ColoredBraceMarker>& braces,
                                       std::vector<EditorDecoration>* out);

void collect_diagnostic_decorations(int line_index, const std::vector<Diagnostic>& diagnostics,
                                    std::vector<EditorDecoration>* out);

ftxui::Element RenderEditorLine(const std::string& line, int line_index,
                               const EditorBuffer& buffer, bool editor_focused,
                               const std::vector<TextMatch>* find_matches = nullptr,
                               const std::vector<TextMatch>* selection_occurrences = nullptr,
                               const SemanticTokenDocument* semantic_tokens = nullptr,
                               const BracketPairHighlight* bracket = nullptr,
                               const BracketPairHighlight* scope_bracket = nullptr,
                               int scope_highlight_strength = 58,
                               const std::vector<Diagnostic>* line_diagnostics = nullptr,
                               const std::string* diagnostic_suffix = nullptr,
                               const std::vector<Diagnostic>* suffix_diagnostics = nullptr,
                               const EditorSymbolPress* symbol_press = nullptr,
                               bool show_caret = true, int scroll_col = 0, int view_width = -1,
                               SyntaxHighlightContext* highlight_ctx = nullptr,
                               bool sticky_scroll_line = false,
                               bool indent_guides_enabled = false, int indent_guide_depth = 0,
                               bool defer_rich_decorations = false,
                               ftxui::Color cursor_cell_bg = theme::CursorCell(),
                               const std::vector<ColoredBraceMarker>* colored_braces = nullptr);

}  // namespace tgdb
