#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ftxui/dom/elements.hpp"
#include "editor/editor_folds.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "editor/bracket_match.hpp"
#include "symbols/hover_info.hpp"
#include "symbols/symbol_provider.hpp"

namespace tgdb {

class TreeSitterService {
 public:
  static TreeSitterService& instance();

  using ReadyCallback = std::function<void(const std::string& path)>;
  void set_ready_callback(ReadyCallback callback);

  void prepare_document(const std::string& path, const std::string& source,
                        const std::optional<EditorTextEditHint>& edit_hint = std::nullopt);
  bool document_ready(const std::string& path, const std::string& source) const;

  std::vector<SymbolInfo> symbols_for_file(const std::string& path, const std::string& source);
  std::vector<SymbolInfo> symbols_for_buffer(const std::string& path,
                                               const std::vector<std::string>& lines);

  const std::vector<LineHighlights>* highlights_for(const std::string& path,
                                                      const std::string& source);
  // Cached highlights only; never schedules tree-sitter prepare/parse.
  const std::vector<LineHighlights>* stale_highlights_for(const std::string& path);
  const std::vector<LineHighlights>* stale_highlights_for(const std::string& path,
                                                           int line_count);
  // Sync highlight query for the active editing line only (uses the live tree after
  // ts_tree_edit; never schedules parse). Returns nullopt when the tree is unavailable.
  std::optional<LineHighlights> highlights_for_editing_line(const std::string& path,
                                                              const std::string& source,
                                                              int line_0);
  // True while sync edits have dirtied the cached per-line highlights and the worker
  // refresh has not committed yet.
  bool highlights_refresh_pending(const std::string& path) const;
  bool document_highlights_ready(const std::string& path, const std::string& source) const;
  void ensure_viewport_preview(const std::string& path, const std::string& source,
                             const std::vector<int>& line_indices);
  const LineHighlights* viewport_preview_line(const std::string& path, const std::string& source,
                                              int line_0) const;
  // Patch one line of the frozen baseline from the live tree (called when typing settles).
  void commit_line_highlights(const std::string& path, const std::string& source, int line_0,
                              const std::string& line_text);
  ftxui::Element highlight_line(const std::string& path, const std::string& source, int line_index,
                                int cursor_col = -1, ftxui::Decorator cursor_style = {},
                                int col_offset = 0);
  ftxui::Element highlight_line(const std::string& path, const std::vector<std::string>& lines,
                                int line_index, int cursor_col = -1,
                                ftxui::Decorator cursor_style = {}, int col_offset = 0);

  std::vector<SymbolInfo> scope_chain_at(const std::string& path,
                                         const std::vector<std::string>& lines,
                                         int line_0based);
  ScopeLineRange innermost_scope_range_at(const std::string& path,
                                          const std::vector<std::string>& lines,
                                          int line_0based, int col_0based);
  BracketPairHighlight scope_bracket_pair_at(const std::string& path,
                                             const std::vector<std::string>& lines,
                                             int line_0based, int col_0based);
  std::vector<FoldRegion> fold_regions_at(const std::string& path,
                                          const std::vector<std::string>& lines);
  std::vector<ColoredBraceMarker> colored_curly_braces_at(const std::string& path,
                                                          const std::vector<std::string>& lines);

  BracketPairHighlight bracket_pair_highlight(const std::string& path, const std::string& source,
                                              int line, int col);
  BracketPairHighlight enclosing_bracket_pair(const std::string& path, const std::string& source,
                                              int line, int col, char open_ch);
  BracketPairHighlight enclosing_bracket_pair_for_block_nav(const std::string& path,
                                                            const std::string& source, int line,
                                                            int col, char open_ch,
                                                            bool jump_to_start);
  TextSpan enclosing_quote_pair(const std::string& path, const std::string& source, int line,
                                int col, char quote_ch);
  TextSpan enclosing_line_comment(const std::string& path, const std::string& source, int line,
                                  int col);
  TextSpan enclosing_block_comment(const std::string& path, const std::string& source, int line,
                                   int col);
  bool cursor_in_code(const std::string& path, const std::string& source, int line, int col);

  std::vector<CompletionItem> local_completions_at(const CompletionParams& params);
  HoverInfo hover_at(const HoverParams& params);

  void invalidate(const std::string& path);
  uint64_t revision_for(const std::string& path) const;

 private:
  TreeSitterService() = default;
  std::string cache_key_for(const std::string& path) const;
  DocumentPtr document_for(const std::string& path, const std::string& source);
  TSNode cached_root_for(const std::string& path, const std::string& source);

  mutable std::mutex mutex_;
  TreeSitterDocumentCache cache_;
};

inline TreeSitterService& tree_sitter_service() { return TreeSitterService::instance(); }

}  // namespace tgdb
