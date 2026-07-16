#include "parser/tree_sitter_service.hpp"

#include <algorithm>
#include <cctype>

#include "editor/editor_context.hpp"
#include "parser/tree_sitter_blocks.hpp"
#include "parser/tree_sitter_completion.hpp"
#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_symbols.hpp"

#include "util/csv_viewer.hpp"

namespace tgdb {

namespace {

std::string word_at_line_col_local(const std::string& line, int col) {
  if (col < 0 || line.empty()) {
    return {};
  }
  int start = std::min(col, static_cast<int>(line.size()) - 1);
  while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[static_cast<std::size_t>(start - 1)])) ||
                       line[static_cast<std::size_t>(start - 1)] == '_')) {
    --start;
  }
  int end = start;
  while (end < static_cast<int>(line.size()) &&
         (std::isalnum(static_cast<unsigned char>(line[static_cast<std::size_t>(end)])) ||
          line[static_cast<std::size_t>(end)] == '_')) {
    ++end;
  }
  return line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

}  // namespace

TreeSitterService& TreeSitterService::instance() {
  static TreeSitterService service;
  return service;
}

void TreeSitterService::set_ready_callback(ReadyCallback callback) {
  cache_.set_ready_callback(std::move(callback));
}

std::string TreeSitterService::cache_key_for(const std::string& path) const {
  if (path.empty()) {
    return "__buffer__";
  }
  return path;
}

void TreeSitterService::prepare_document(const std::string& path, const std::string& source,
                                         const std::optional<EditorTextEditHint>& edit_hint) {
  const std::string canonical = normalize_editor_source(source);
  if (canonical.empty()) {
    return;
  }
  cache_.request_prepare(cache_key_for(path), canonical, edit_hint);
}

bool TreeSitterService::document_ready(const std::string& path, const std::string& source) const {
  const std::string canonical = normalize_editor_source(source);
  DocumentPtr doc = cache_.lookup(cache_key_for(path));
  return doc != nullptr && doc->source == canonical && doc->parse_ready;
}

DocumentPtr TreeSitterService::document_for(const std::string& path, const std::string& source) {
  const std::string key = cache_key_for(path);
  cache_.request_prepare(key, source);
  return cache_.lookup(key);
}

TSNode TreeSitterService::cached_root_for(const std::string& path, const std::string& source) {
  const std::string canonical = normalize_editor_source(source);
  const std::string key = cache_key_for(path);
  cache_.request_prepare(key, canonical);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->source != canonical || !doc->parse_ready || doc->tree == nullptr) {
    return TSNode{};
  }
  return ts_tree_root_node(doc->tree);
}

std::vector<SymbolInfo> TreeSitterService::symbols_for_file(const std::string& path,
                                                              const std::string& source) {
  if (path.empty() || is_tabular_path(path) || source.empty()) {
    return {};
  }
  const std::string canonical = normalize_editor_source(source);
  const std::string key = cache_key_for(path);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->source != canonical) {
    cache_.request_prepare(key, canonical);
    return {};
  }
  if (!doc->symbols_ready) {
    cache_.request_prepare(key, canonical);
    return {};
  }
  return doc->symbols;
}

std::vector<SymbolInfo> TreeSitterService::symbols_for_buffer(
    const std::string& path, const std::vector<std::string>& lines) {
  return symbols_for_file(path, join_editor_lines(lines));
}

namespace {

int source_line_count(const std::string& source) {
  if (source.empty()) {
    return 1;
  }
  return static_cast<int>(std::count(source.begin(), source.end(), '\n') + 1);
}

TSTree* parse_tree_for_source(TSTree* old_tree, const std::string& source,
                              const std::string& path) {
  if (source.empty()) {
    return nullptr;
  }
  const TSLanguage* language = tree_sitter_language_for_path(path);
  if (language == nullptr) {
    language = tree_sitter_cpp_language();
  }
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, language);
  TSTree* reparsed =
      ts_parser_parse_string(parser, old_tree, source.c_str(), static_cast<uint32_t>(source.size()));
  ts_parser_delete(parser);
  return reparsed;
}

}  // namespace

const std::vector<LineHighlights>* TreeSitterService::highlights_for(const std::string& path,
                                                                        const std::string& source) {
  const std::string canonical = normalize_editor_source(source);
  if (canonical.empty()) {
    return nullptr;
  }
  const std::string key = cache_key_for(path);
  cache_.request_prepare(key, canonical);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr) {
    return nullptr;
  }
  const int line_count = source_line_count(canonical);
  if (doc->source == canonical && !doc->line_highlights.empty()) {
    if (static_cast<int>(doc->line_highlights.size()) == line_count) {
      return &doc->line_highlights;
    }
    if (!doc->highlights_ready) {
      return &doc->line_highlights;
    }
    return nullptr;
  }
  if (doc->source != canonical || !doc->highlights_ready) {
    return nullptr;
  }
  return &doc->line_highlights;
}

const std::vector<LineHighlights>* TreeSitterService::stale_highlights_for(
    const std::string& path) {
  const std::string key = cache_key_for(path);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->line_highlights.empty()) {
    return nullptr;
  }
  return &doc->line_highlights;
}

const std::vector<LineHighlights>* TreeSitterService::stale_highlights_for(
    const std::string& path, int line_count) {
  const std::string key = cache_key_for(path);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->line_highlights.empty() || line_count <= 0) {
    return nullptr;
  }
  if (static_cast<int>(doc->line_highlights.size()) == line_count) {
    return &doc->line_highlights;
  }
  // Worker refresh pending: prefer stale colors over a full plain-text flash.
  if (!doc->highlights_ready) {
    return &doc->line_highlights;
  }
  return nullptr;
}

std::optional<LineHighlights> TreeSitterService::highlights_for_editing_line(
    const std::string& path, const std::string& source, int line_0) {
  if (path.empty() || line_0 < 0 || source.empty()) {
    return std::nullopt;
  }
  const std::string canonical = normalize_editor_source(source);
  if (canonical.empty()) {
    return std::nullopt;
  }
  const std::string key = cache_key_for(path);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || !doc->parse_ready || doc->tree == nullptr) {
    return std::nullopt;
  }
  const int doc_line_count = source_line_count(doc->source);
  if (line_0 >= doc_line_count) {
    return std::nullopt;
  }
  TSTree* query_tree = doc->tree;
  TSTree* reparsed_tree = nullptr;
  const bool needs_reparse = !doc->highlights_ready;
  if (needs_reparse) {
    reparsed_tree = parse_tree_for_source(doc->tree, doc->source, path);
    if (reparsed_tree != nullptr) {
      query_tree = reparsed_tree;
    }
  }
  const TSNode root = ts_tree_root_node(query_tree);
  if (ts_node_is_null(root)) {
    if (reparsed_tree != nullptr) {
      ts_tree_delete(reparsed_tree);
    }
    return std::nullopt;
  }
  const TreeSitterLangKind lang = tree_sitter_lang_kind_for_path(path);
  LineHighlights result = highlights_for_line(
      root, doc->source, line_0,
      lang == TreeSitterLangKind::kNone ? TreeSitterLangKind::kCpp : lang);
  const bool line_has_text = [&]() {
    std::size_t pos = 0;
    for (int row = 0; row < line_0; ++row) {
      const std::size_t next = doc->source.find('\n', pos);
      if (next == std::string::npos) {
        return false;
      }
      pos = next + 1;
    }
    const std::size_t end = doc->source.find('\n', pos);
    return (end == std::string::npos ? doc->source.size() : end) > pos;
  }();
  bool did_full_reparse = false;
  if (result.spans.empty() && line_has_text) {
    if (reparsed_tree != nullptr) {
      ts_tree_delete(reparsed_tree);
      reparsed_tree = nullptr;
    }
    TSTree* fresh_tree = parse_tree_for_source(nullptr, doc->source, path);
    if (fresh_tree != nullptr) {
      const TSNode fresh_root = ts_tree_root_node(fresh_tree);
      if (!ts_node_is_null(fresh_root)) {
        result = highlights_for_line(
            fresh_root, doc->source, line_0,
            lang == TreeSitterLangKind::kNone ? TreeSitterLangKind::kCpp : lang);
        did_full_reparse = true;
      }
      ts_tree_delete(fresh_tree);
    }
  }
  const bool did_reparse = reparsed_tree != nullptr || did_full_reparse;
  if (reparsed_tree != nullptr) {
    ts_tree_delete(reparsed_tree);
  }
  return result;
}

bool TreeSitterService::highlights_refresh_pending(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  const DocumentPtr doc = cache_.lookup(cache_key_for(path));
  return doc != nullptr && doc->parse_ready && !doc->highlights_ready;
}

bool TreeSitterService::document_highlights_ready(const std::string& path,
                                                const std::string& source) const {
  if (path.empty() || source.empty()) {
    return false;
  }
  return cache_.document_highlights_ready(cache_key_for(path), normalize_editor_source(source));
}

bool TreeSitterService::document_symbols_ready(const std::string& path,
                                               const std::string& source) const {
  if (path.empty() || source.empty()) {
    return false;
  }
  return cache_.document_symbols_ready(cache_key_for(path), normalize_editor_source(source));
}

void TreeSitterService::ensure_viewport_preview(const std::string& path, const std::string& source,
                                                const std::vector<int>& line_indices) {
  if (path.empty() || source.empty() || line_indices.empty()) {
    return;
  }
  cache_.ensure_viewport_preview(cache_key_for(path), normalize_editor_source(source),
                                 line_indices);
}

void TreeSitterService::ensure_viewport_preview_slice(const std::string& path, int first_line,
                                                      int last_line, const std::string& slice) {
  if (path.empty() || slice.empty()) {
    return;
  }
  cache_.ensure_viewport_preview_slice(cache_key_for(path), first_line, last_line, slice);
}

const LineHighlights* TreeSitterService::viewport_preview_line(const std::string& path,
                                                               const std::string& source,
                                                               int line_0) const {
  if (path.empty() || source.empty() || line_0 < 0) {
    return nullptr;
  }
  return cache_.viewport_preview_line(cache_key_for(path), normalize_editor_source(source),
                                      line_0);
}

const LineHighlights* TreeSitterService::viewport_preview_line_virtual(const std::string& path,
                                                                       int line_0) const {
  if (path.empty() || line_0 < 0) {
    return nullptr;
  }
  return cache_.viewport_preview_line_virtual(cache_key_for(path), line_0);
}

void TreeSitterService::mark_document_viewport_only(const std::string& path) {
  if (path.empty()) {
    return;
  }
  cache_.mark_document_viewport_only(cache_key_for(path));
}

bool TreeSitterService::document_viewport_only(const std::string& path) const {
  if (path.empty()) {
    return false;
  }
  return cache_.document_viewport_only(cache_key_for(path));
}

namespace {

bool highlight_spans_map_to_line(const LineHighlights& highlights, const std::string& line_text) {
  if (line_text.empty()) {
    return highlights.spans.empty();
  }
  for (const HighlightSpan& span : highlights.spans) {
    if (span.end_col > span.start_col && span.start_col < static_cast<int>(line_text.size())) {
      return true;
    }
  }
  return false;
}

}  // namespace

void TreeSitterService::commit_line_highlights(const std::string& path, const std::string& source,
                                                int line_0, const std::string& line_text) {
  if (path.empty() || line_0 < 0 || source.empty()) {
    return;
  }
  const std::optional<LineHighlights> live = highlights_for_editing_line(path, source, line_0);
  if (!live.has_value() || live->spans.empty()) {
    return;
  }
  if (!highlight_spans_map_to_line(*live, line_text)) {
    return;
  }
  const DocumentPtr doc = cache_.lookup(cache_key_for(path));
  if (doc == nullptr) {
    return;
  }
  const std::string canonical = normalize_editor_source(source);
  const int line_count = source_line_count(canonical);
  if (line_0 >= line_count) {
    return;
  }
  if (static_cast<int>(doc->line_highlights.size()) < line_count) {
    doc->line_highlights.resize(static_cast<std::size_t>(line_count));
  }
  doc->line_highlights[static_cast<std::size_t>(line_0)] = *live;
}

bool TreeSitterService::line_highlights_trustworthy_for_line(const std::string& path,
                                                              const std::string& source, int line_0,
                                                              const std::string& line_text) const {
  if (path.empty() || line_0 < 0 || source.empty()) {
    return false;
  }
  if (line_text.empty()) {
    return true;
  }
  const std::string canonical = normalize_editor_source(source);
  const DocumentPtr doc = cache_.lookup(cache_key_for(path));
  if (doc == nullptr || doc->source != canonical || doc->line_highlights.empty() ||
      line_0 >= static_cast<int>(doc->line_highlights.size())) {
    return false;
  }
  return highlight_spans_map_to_line(doc->line_highlights[static_cast<std::size_t>(line_0)],
                                     line_text);
}

ftxui::Element TreeSitterService::highlight_line(const std::string& path,
                                                 const std::string& source, int line_index,
                                                 int cursor_col, ftxui::Decorator cursor_style,
                                                 int col_offset) {
  if (line_index < 0) {
    return ftxui::text("");
  }
  const auto* all_highlights = highlights_for(path, source);
  if (all_highlights == nullptr || line_index >= static_cast<int>(all_highlights->size())) {
    return ftxui::text("");
  }
  std::size_t begin = 0;
  for (int row = 0; row < line_index; ++row) {
    const std::size_t next = source.find('\n', begin);
    if (next == std::string::npos) {
      return ftxui::text("");
    }
    begin = next + 1;
  }
  const std::size_t end = source.find('\n', begin);
  const std::string line =
      source.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
  return HighlightTreeSitterLine(line, line_index,
                                 (*all_highlights)[static_cast<std::size_t>(line_index)], cursor_col,
                                 cursor_style, col_offset);
}

ftxui::Element TreeSitterService::highlight_line(const std::string& path,
                                                 const std::vector<std::string>& lines,
                                                 int line_index, int cursor_col,
                                                 ftxui::Decorator cursor_style, int col_offset) {
  return highlight_line(path, join_editor_lines(lines), line_index, cursor_col, cursor_style,
                        col_offset);
}

std::vector<SymbolInfo> TreeSitterService::scope_chain_at(const std::string& path,
                                                            const std::vector<std::string>& lines,
                                                            int line_0based) {
  const std::string source = join_editor_lines(lines);
  const std::string key = cache_key_for(path);
  cache_.request_prepare(key, source);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->scope_symbols.empty()) {
    return {};
  }
  std::vector<SymbolInfo> chain;
  for (const SymbolInfo* sym : scope_chain_at_point(doc->scope_symbols, line_0based)) {
    chain.push_back(*sym);
  }
  return chain;
}

ScopeLineRange TreeSitterService::innermost_scope_range_at(const std::string& path,
                                                         const std::vector<std::string>& lines,
                                                         int line_0based, int col_0based) {
  const std::string source = join_editor_lines(lines);
  const std::string key = cache_key_for(path);
  cache_.request_prepare(key, source);
  DocumentPtr doc = cache_.lookup(key);
  if (doc == nullptr || doc->scope_symbols.empty()) {
    return {};
  }
  return innermost_scope_range_from_symbols(doc->scope_symbols, line_0based, col_0based);
}

BracketPairHighlight TreeSitterService::scope_bracket_pair_at(const std::string& path,
                                                              const std::vector<std::string>& lines,
                                                              int line_0based, int col_0based) {
  const std::string source = join_editor_lines(lines);
  const TSNode root = cached_root_for(path, source);
  return scope_bracket_pair_from_tree(root, source, line_0based, col_0based);
}

std::vector<FoldRegion> TreeSitterService::fold_regions_at(const std::string& path,
                                                           const std::vector<std::string>& lines) {
  const std::string source = join_editor_lines(lines);
  const TSNode root = cached_root_for(path, source);
  return fold_regions_from_tree(root, source);
}

std::vector<ColoredBraceMarker> TreeSitterService::colored_curly_braces_at(
    const std::string& path, const std::vector<std::string>& lines) {
  const std::string source = join_editor_lines(lines);
  const TSNode root = cached_root_for(path, source);
  return colored_curly_braces(root, source);
}

BracketPairHighlight TreeSitterService::bracket_pair_highlight(const std::string& path,
                                                              const std::string& source, int line,
                                                              int col) {
  const TSNode root = cached_root_for(path, source);
  return bracket_pair_at(root, source, line, col);
}

BracketPairHighlight TreeSitterService::enclosing_bracket_pair(const std::string& path,
                                                             const std::string& source, int line,
                                                             int col, char open_ch) {
  const TSNode root = cached_root_for(path, source);
  return tgdb::enclosing_bracket_pair(root, source, line, col, open_ch);
}

BracketPairHighlight TreeSitterService::enclosing_bracket_pair_for_block_nav(
    const std::string& path, const std::string& source, int line, int col, char open_ch,
    bool jump_to_start) {
  const TSNode root = cached_root_for(path, source);
  return tgdb::enclosing_bracket_pair_for_block_nav(root, source, line, col, open_ch,
                                                    jump_to_start);
}

TextSpan TreeSitterService::enclosing_quote_pair(const std::string& path, const std::string& source,
                                                 int line, int col, char quote_ch) {
  const TSNode root = cached_root_for(path, source);
  return tgdb::enclosing_quote_pair(root, source, line, col, quote_ch);
}

TextSpan TreeSitterService::enclosing_line_comment(const std::string& path,
                                                 const std::string& source, int line, int col) {
  const TSNode root = cached_root_for(path, source);
  return tgdb::enclosing_line_comment(root, source, line, col);
}

TextSpan TreeSitterService::enclosing_block_comment(const std::string& path,
                                                  const std::string& source, int line, int col) {
  const TSNode root = cached_root_for(path, source);
  return tgdb::enclosing_block_comment(root, source, line, col);
}

bool TreeSitterService::cursor_in_code(const std::string& path, const std::string& source, int line,
                                       int col) {
  // While the AST is catching up after edits, don't block editor features (live
  // completion, auto-pairs) that only need a best-effort code/string distinction.
  if (!document_ready(path, source)) {
    return true;
  }
  const TSNode root = cached_root_for(path, source);
  if (ts_node_is_null(root)) {
    return true;
  }
  return cursor_in_code_node(root, source, line, col);
}

std::vector<CompletionItem> TreeSitterService::local_completions_at(
    const CompletionParams& params) {
  DocumentPtr doc = document_for(params.path, params.text);
  if (doc == nullptr || doc->tree == nullptr || !doc->parse_ready) {
    return {};
  }
  return tgdb::local_completions_at(ts_tree_root_node(doc->tree), doc->source, params);
}

HoverInfo TreeSitterService::hover_at(const HoverParams& params) {
  HoverInfo info;
  if (params.path.empty() || params.text.empty()) {
    return info;
  }

  std::istringstream input(params.text);
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) {
    return info;
  }

  const std::string word =
      params.line >= 0 && params.line < static_cast<int>(lines.size())
          ? word_at_line_col_local(lines[static_cast<std::size_t>(params.line)], params.character)
          : std::string{};
  if (word.empty()) {
    return info;
  }

  info.title = word;
  DocumentPtr doc = document_for(params.path, params.text);
  if (doc != nullptr && !doc->scope_symbols.empty()) {
    for (const SymbolInfo* sym : scope_chain_at_point(doc->scope_symbols, params.line)) {
      if (sym->name.find(word) != std::string::npos) {
        info.body_lines.push_back(sym->name + " (L" + std::to_string(sym->line) + ")");
      }
    }
  }
  if (info.body_lines.empty()) {
    info.body_lines.push_back("L" + std::to_string(params.line + 1) + ":" +
                              std::to_string(params.character + 1));
  }
  info.valid = true;
  return info;
}

void TreeSitterService::invalidate(const std::string& path) { cache_.invalidate(cache_key_for(path)); }

uint64_t TreeSitterService::revision_for(const std::string& path) const {
  return cache_.revision_for(cache_key_for(path));
}

TreeSitterService::HighlightTreeSnapshot TreeSitterService::snapshot_for_highlight(
    const std::string& path, const std::string& canonical, uint64_t expected_revision) const {
  return cache_.snapshot_for_highlight(cache_key_for(path), canonical, expected_revision);
}

}  // namespace tgdb
