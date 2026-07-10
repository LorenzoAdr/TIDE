#include "ui/editor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "editor/editor_buffer_source.hpp"
#include "editor/bracket_match.hpp"
#include "editor/clipboard.hpp"
#include "editor/code_snippets.hpp"
#include "editor/editor_folds.hpp"
#include "editor/helix/helix_dispatch.hpp"
#include "editor/helix/helix_hints.hpp"
#include "editor/helix/helix_state.hpp"
#include "git/git_diff.hpp"
#include "git/git_service.hpp"
#include "editor/editor_context.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/editor_render.hpp"
#include "editor/indent_guides.hpp"
#include "editor/selection_occurrence_runner.hpp"
#include "util/clang_format_config.hpp"
#include "parser/tree_sitter_service.hpp"
#include "util/csv_viewer.hpp"
#include "util/tabular_file.hpp"
#include "util/monitor_log.hpp"
#include "util/ui_perf_monitor.hpp"
#include "util/fuzzy_match.hpp"
#include "lsp/diagnostics.hpp"
#include "editor/editor_state.hpp"
#include "editor/line_comment.hpp"
#include "editor/text_ops.hpp"
#include "editor/text_search.hpp"
#include "ftxui/component/component.hpp"
#include "indexer/symbol_workspace_indexer.hpp"
#include "indexer/index_rules.hpp"
#include "symbols/symbol_provider.hpp"
#include "symbols/hover_info.hpp"
#include "symbols/completion_snippet.hpp"
#include "symbols/symbol_utils.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "i18n/tr.hpp"
#include "ui/clickable.hpp"
#include "ui/hover_effects.hpp"
#include "ui/editor_tab_bar.hpp"
#include "ui/press_ids.hpp"
#include "ui/focusable_component.hpp"
#include "ui/context_menu.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/glyphs.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/welcome_screen.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/overview_ruler.hpp"
#include "ui/text_input_style.hpp"
#include "ui/spinner.hpp"
#include "ui/source_panel.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

struct EditorPanelState;

std::string buffer_text(const EditorBuffer& buffer) {
  std::string text;
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    if (i > 0) {
      text.push_back('\n');
    }
    text += buffer.lines[i];
  }
  return text;
}

void mark_editor_content_edited(EditorPanelState* panel, EditorBuffer& buffer);

void notify_editor_buffer_changed(WorkspaceModel* workspace, EditorPanelState* panel,
                                  const std::shared_ptr<ISymbolProvider>& symbols) {
  if (workspace == nullptr) {
    return;
  }
  workspace->ensure_buffer();
  EditorBuffer& buffer = workspace->buffer;
  if (buffer.path.empty()) {
    return;
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  workspace->last_buffer_edit_ms = now;
  buffer.view_token++;
  if (panel != nullptr) {
    mark_editor_content_edited(panel, buffer);
  }
  if (panel == nullptr && symbols != nullptr) {
    symbols->on_document_changed(buffer.path, buffer_text(buffer));
  }
}

struct BreadcrumbHit {
  int x_min = 0;
  int x_max = 0;
  int line = 0;
};

struct EditorHoverState {
  bool visible = false;
  bool click_triggered = false;
  int line = -1;
  int col = -1;
  int anchor_x = 0;
  int anchor_y = 0;
  int64_t dwell_start_ms = 0;
  std::string fetch_key;
  HoverInfo info;
};

struct DiagnosticModalState {
  bool open = false;
  int line = 0;
  std::vector<Diagnostic> items;
};

struct GitHistoryModalState {
  bool open = false;
  int line = 0;
  std::string path;
  std::string previous_content;
  std::string current_content;
};

struct CachedViewportLineRow {
  uint64_t key = 0;
  Element gutter;
  Element code;
};

struct EditorPanelState {
  FocusRegion panel_focus = FocusRegion::Editor;
  Box code_box;
  Box gutter_box;
  Box breadcrumb_box;
  Box problems_button_box;
  Box scrollbar_box;
  Box h_scrollbar_box;
  Box overview_ruler_box;
  ScrollbarLayout scrollbar_layout;
  HorizontalScrollbarLayout h_scrollbar_layout;
  OverviewRulerLayout overview_ruler_layout;
  bool scrollbar_dragging = false;
  bool h_scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  int h_scrollbar_drag_offset = 0;
  uint64_t last_view_token = 0;
  int last_render_caret_line = -1;
  std::string last_path;
  bool mouse_selecting = false;
  CapturedMouse captured_mouse;
  int last_click_line = -1;
  int last_click_col = -1;
  int64_t last_click_ms = 0;
  int last_click_count = 0;
  bool line_select_drag = false;
  int line_select_anchor = -1;
  int line_select_commit_line = -1;
  bool word_select_drag = false;
  int word_select_anchor_line = -1;
  int word_select_anchor_col = -1;
  std::vector<BreadcrumbHit> breadcrumb_hits;
  std::vector<BreadcrumbItem> breadcrumbs;
  std::vector<SymbolInfo> cached_symbols;
  std::string cached_symbols_path;
  EditorHoverState hover;
  uint64_t bracket_cache_token = 0;
  int bracket_cache_line = -1;
  int bracket_cache_col = -1;
  BracketPairHighlight bracket_cache;
  uint64_t scope_bracket_cache_token = 0;
  int scope_bracket_cache_line = -1;
  int scope_bracket_cache_col = -1;
  BracketPairHighlight scope_bracket_cache;
  std::vector<ColoredBraceMarker> colored_brace_cache;
  std::string colored_brace_cache_path;
  uint64_t colored_brace_cache_token = 0;
  uint64_t last_diag_revision = 0;
  std::string last_diag_path;
  int problem_errors = 0;
  int problem_warnings = 0;
  DocumentDiagnostics cached_file_diag;
  uint64_t cached_file_diag_revision = 0;
  int gutter_scroll_start = 0;
  int gutter_visible_rows = 0;
  int gutter_fold_width = 0;
  std::vector<int> viewport_line_cache;
  std::string fold_regions_cache_path;
  uint64_t fold_regions_cache_token = 0;
  bool symbols_fetch_pending = false;
  uint64_t last_document_symbols_revision = 0;
  bool semantic_tokens_enqueue_pending = false;
  bool semantic_tokens_layout_stale = false;
  uint64_t last_semantic_highlight_revision_tick = 0;
  SemanticTokenDocument cached_semantic_tokens;
  std::string cached_semantic_path;
  uint64_t last_semantic_highlight_revision = 0;
  int code_width_chars = 80;
  int64_t content_edit_ms = 0;
  int64_t last_heavy_editor_tick_ms = 0;
  bool lsp_sync_pending = false;
  std::unordered_map<int, std::vector<Diagnostic>> diagnostics_by_line;
  uint64_t diagnostics_by_line_revision = 0;
  std::string diagnostics_by_line_path;
  std::unordered_map<int, std::string> diagnostic_suffix_by_line;
  int diagnostic_suffix_code_width = 0;
  uint64_t diagnostic_suffix_view_token = 0;
  uint64_t diagnostic_suffix_revision = 0;
  std::unordered_set<int> git_changed_lines;
  std::unordered_map<int, std::string> git_previous_by_line;
  bool git_untracked_all_lines = false;
  int git_cached_line_count = 0;
  std::string git_cache_path;
  uint64_t git_cache_revision = 0;
  uint64_t git_cache_view_token = 0;
  int last_render_scroll = 0;
  int64_t last_scroll_change_ms = 0;
  IndentGuideTracker guide_tracker_cache;
  int guide_tracker_scroll = -1;
  int guide_tracker_tab_width = 0;
  uint64_t guide_tracker_view_token = 0;
  bool document_open_pending = false;
  std::string pending_document_open_path;
  std::vector<TextMatch> selection_occurrence_matches;
  SelectionOccurrenceKey selection_occurrence_committed_key;
  SelectionOccurrenceRunner selection_occurrence_runner;
  uint64_t selection_occurrence_request_counter = 0;
  uint64_t selection_occurrence_inflight_id = 0;
  bool chord_k_pending = false;
  HelixEditorState helix;
  std::string tabular_layout_path;
  TabularDelimiter tabular_delimiter = TabularDelimiter::kComma;
  TabularTableLayout tabular_layout;
  int tabular_layout_line_count = 0;
  std::unique_ptr<TabularFileStore> tabular_store;
  int64_t last_tabular_index_tick_ms = 0;
  bool tabular_scroll_locked = false;
  int tabular_scroll_limit = 0;
  std::unordered_map<int, CachedViewportLineRow> viewport_line_render_cache;
  std::unordered_map<uint64_t, CachedSyntaxLineSpans> line_syntax_span_cache;
  int line_syntax_span_cache_scroll_col = -1;
  uint64_t line_syntax_span_cache_ts_revision = 0;
  uint64_t viewport_line_render_cache_ts_revision = 0;
  std::string scope_completion_cache_key;
  std::vector<CompletionItem> scope_completion_cache_items;
};

void flash_symbol_at_buffer_pos_impl(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                     EditorPanelState* panel_state, int line, int col,
                                     int visible_lines);

constexpr int kDoubleClickMs = 400;
constexpr int kHoverDelayMs = 500;

static bool hover_key_same_position(std::string_view fetch_key, std::string_view path, int line,
                                    int col) {
  const std::string prefix =
      std::string(path) + "|" + std::to_string(line) + "|" + std::to_string(col) + "|";
  return fetch_key.size() >= prefix.size() &&
         fetch_key.compare(0, prefix.size(), prefix) == 0;
}

constexpr int kSuffixScrollSettleMs = 150;
constexpr int kEditorContentSettleMs = 120;
constexpr int kHeavyEditorTickIntervalMs = 200;
constexpr int kLiveCompletionDebounceMs = 120;
constexpr int kLiveCompletionMinIntervalMs = 50;
constexpr int kLiveCompletionLspWaitTimeoutMs = 2500;
constexpr int kLiveCompletionLspWaitMaxMs = 45000;
constexpr int kLiveCompletionInflightMaxMs = 35000;

int live_completion_lsp_wait_timeout_ms(const std::string& text, int line) {
  const int64_t scaled = static_cast<int64_t>(text.size()) / 500;
  const int64_t line_bonus = static_cast<int64_t>(std::max(0, line)) * 25;
  const int64_t client_wait = std::clamp<int64_t>(800 + scaled + line_bonus, 800, 8000);
  constexpr int kRpcSlackMs = 5000;
  return static_cast<int>(
      std::clamp<int64_t>(client_wait + kRpcSlackMs, kLiveCompletionLspWaitTimeoutMs,
                          kLiveCompletionLspWaitMaxMs));
}

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

constexpr uint64_t kViewportLineHashOffset = 14695981039346656037ULL;
constexpr uint64_t kViewportLineHashPrime = 1099511628211ULL;

uint64_t viewport_line_hash_u64(uint64_t h, uint64_t v) {
  h ^= v;
  h *= kViewportLineHashPrime;
  return h;
}

uint64_t viewport_line_hash_int(uint64_t h, int v) {
  return viewport_line_hash_u64(h, static_cast<uint64_t>(v));
}

uint64_t viewport_line_hash_bool(uint64_t h, bool v) {
  return viewport_line_hash_u64(h, v ? 1 : 0);
}

uint64_t viewport_line_hash_string(uint64_t h, std::string_view s) {
  for (unsigned char c : s) {
    h = viewport_line_hash_u64(h, c);
  }
  return h;
}

uint64_t viewport_line_hash_matches_on_line(uint64_t h, const std::vector<TextMatch>* matches,
                                            int line_index) {
  if (matches == nullptr) {
    return h;
  }
  for (const TextMatch& match : *matches) {
    if (match.line != line_index) {
      continue;
    }
    h = viewport_line_hash_int(h, match.col);
    h = viewport_line_hash_int(h, match.length);
  }
  return h;
}

uint64_t viewport_line_hash_selection_on_line(uint64_t h, const EditorBuffer& buffer,
                                              int line_index) {
  const MultiCursor& cursor = buffer.primary();
  if (!cursor.has_selection()) {
    return h;
  }
  const int lo = std::min(cursor.anchor.line, cursor.head.line);
  const int hi = std::max(cursor.anchor.line, cursor.head.line);
  if (line_index < lo || line_index > hi) {
    return h;
  }
  h = viewport_line_hash_int(h, cursor.anchor.line);
  h = viewport_line_hash_int(h, cursor.anchor.col);
  h = viewport_line_hash_int(h, cursor.head.line);
  h = viewport_line_hash_int(h, cursor.head.col);
  return h;
}

uint64_t viewport_line_hash_diagnostics(uint64_t h, const std::vector<Diagnostic>* diagnostics) {
  if (diagnostics == nullptr) {
    return h;
  }
  for (const Diagnostic& diag : *diagnostics) {
    h = viewport_line_hash_int(h, static_cast<int>(diag.severity));
    h = viewport_line_hash_int(h, diag.start_col);
    h = viewport_line_hash_int(h, diag.end_col);
  }
  return h;
}

struct ViewportLineRenderKeyInput {
  int line_index = -1;
  std::string_view line_content;
  int guide_depth = 0;
  char fold_marker = '\0';
  char gutter_marker = ' ';
  bool has_breakpoint = false;
  bool in_immediate_scope_gutter = false;
  const std::string* suffix_ptr = nullptr;
  bool symbol_press_active = false;
  const std::vector<Diagnostic>* line_diagnostics = nullptr;
  const std::vector<TextMatch>* find_matches = nullptr;
  const std::vector<TextMatch>* selection_occurrences = nullptr;
};

uint64_t compute_viewport_line_render_key(const ViewportLineRenderKeyInput& line,
                                        const EditorBuffer& buffer, const EditorPanelState& panel,
                                        bool editor_focused, bool show_caret, bool mouse_selecting,
                                        bool helix_caret_insert, int scroll_col, int code_width,
                                        bool helix_relative, int gutter_w, bool fold_gutter_enabled,
                                        bool gutter_markers, bool indent_guides_enabled,
                                        int scope_highlight_strength, bool typing_burst,
                                        const BracketPairHighlight& bracket,
                                        const BracketPairHighlight* scope_bracket,
                                        uint64_t colored_brace_token, uint64_t semantic_revision,
                                        uint64_t semantic_source_generation, bool git_line_changed,
                                        uint64_t ts_revision) {
  uint64_t h = kViewportLineHashOffset;
  h = viewport_line_hash_int(h, line.line_index);
  h = viewport_line_hash_string(h, line.line_content);
  if (line.line_index == buffer.primary_line()) {
    h = viewport_line_hash_int(h, buffer.primary_col());
  }
  h = viewport_line_hash_int(h, scroll_col);
  h = viewport_line_hash_int(h, code_width);
  h = viewport_line_hash_int(h, line.guide_depth);
  h = viewport_line_hash_bool(h, editor_focused);
  h = viewport_line_hash_bool(h, show_caret);
  h = viewport_line_hash_bool(h, mouse_selecting);
  h = viewport_line_hash_bool(h, helix_caret_insert);
  h = viewport_line_hash_bool(h, helix_relative);
  h = viewport_line_hash_int(h, gutter_w);
  h = viewport_line_hash_bool(h, fold_gutter_enabled);
  h = viewport_line_hash_int(h, line.fold_marker);
  h = viewport_line_hash_bool(h, gutter_markers);
  h = viewport_line_hash_bool(h, line.has_breakpoint);
  h = viewport_line_hash_int(h, line.gutter_marker);
  h = viewport_line_hash_bool(h, line.in_immediate_scope_gutter);
  h = viewport_line_hash_bool(h, indent_guides_enabled);
  h = viewport_line_hash_int(h, scope_highlight_strength);
  h = viewport_line_hash_bool(h, typing_burst);
  h = viewport_line_hash_u64(h, ts_revision);
  h = viewport_line_hash_bool(h, git_line_changed);
  h = viewport_line_hash_bool(h, line.symbol_press_active);
  h = viewport_line_hash_u64(h, colored_brace_token);
  h = viewport_line_hash_u64(h, semantic_revision);
  h = viewport_line_hash_u64(h, semantic_source_generation);
  h = viewport_line_hash_string(h, buffer.path);
  if (line.suffix_ptr != nullptr) {
    h = viewport_line_hash_string(h, *line.suffix_ptr);
  }
  h = viewport_line_hash_matches_on_line(h, line.find_matches, line.line_index);
  h = viewport_line_hash_matches_on_line(h, line.selection_occurrences, line.line_index);
  h = viewport_line_hash_selection_on_line(h, buffer, line.line_index);
  h = viewport_line_hash_diagnostics(h, line.line_diagnostics);
  if (bracket.valid) {
    h = viewport_line_hash_int(h, bracket.line_a);
    h = viewport_line_hash_int(h, bracket.line_b);
    h = viewport_line_hash_int(h, bracket.col_a);
    h = viewport_line_hash_int(h, bracket.col_b);
  }
  if (scope_bracket != nullptr && scope_bracket->valid) {
    h = viewport_line_hash_int(h, scope_bracket->line_a);
    h = viewport_line_hash_int(h, scope_bracket->line_b);
    h = viewport_line_hash_int(h, scope_bracket->col_a);
    h = viewport_line_hash_int(h, scope_bracket->col_b);
  }
  return h;
}

void prune_viewport_line_render_cache(EditorPanelState* panel,
                                      const std::vector<int>& visible_lines) {
  if (panel == nullptr) {
    return;
  }
  std::unordered_set<int> visible(visible_lines.begin(), visible_lines.end());
  for (auto it = panel->viewport_line_render_cache.begin();
       it != panel->viewport_line_render_cache.end();) {
    if (visible.count(it->first) == 0) {
      it = panel->viewport_line_render_cache.erase(it);
    } else {
      ++it;
    }
  }
}

bool editor_content_settled(const EditorPanelState& panel) {
  return steady_now_ms() - panel.content_edit_ms >= kEditorContentSettleMs;
}

bool semantic_tokens_match_document(const ISymbolProvider& symbols, const std::string& path,
                                    const SemanticTokenDocument& tokens) {
  if (!tokens.ready || path.empty()) {
    return false;
  }
  const uint64_t doc_gen = symbols.document_generation_for_file(path);
  return doc_gen > 0 && tokens.source_generation == doc_gen;
}

void mark_editor_content_edited(EditorPanelState* panel, EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }
  panel->content_edit_ms = steady_now_ms();
  panel->guide_tracker_view_token = 0;
  if (buffer.semantic_layout_dirty) {
    panel->semantic_tokens_layout_stale = true;
    panel->semantic_tokens_enqueue_pending = true;
    panel->viewport_line_render_cache.clear();
    panel->line_syntax_span_cache.clear();
    buffer.semantic_layout_dirty = false;
  }
  if (is_indexed_source_path(buffer.path)) {
    panel->lsp_sync_pending = true;
    tree_sitter_service().prepare_document(buffer.path, editor_buffer_joined_source(buffer));
  }
}

void sync_guide_tracker_cache(EditorPanelState* panel, const EditorBuffer& buffer, int scroll_start,
                              int tab_col_width) {
  if (panel == nullptr) {
    return;
  }
  const bool needs_rebuild = panel->guide_tracker_scroll != scroll_start ||
                             panel->guide_tracker_tab_width != tab_col_width ||
                             panel->guide_tracker_view_token != buffer.view_token;
  if (!needs_rebuild) {
    return;
  }

  panel->guide_tracker_cache.reset();
  for (int i = 0; i < scroll_start && i < static_cast<int>(buffer.lines.size()); ++i) {
    panel->guide_tracker_cache.advance(buffer.lines[static_cast<std::size_t>(i)], tab_col_width);
  }
  panel->guide_tracker_scroll = scroll_start;
  panel->guide_tracker_tab_width = tab_col_width;
  panel->guide_tracker_view_token = buffer.view_token;
}

CursorPos mouse_to_cursor(const Mouse& m, const EditorPanelState& panel, const EditorBuffer& buffer,
                          int visible_lines);
void end_mouse_selection(EditorPanelState* panel);

void clear_hover_state(EditorHoverState* hover) {
  if (hover == nullptr) {
    return;
  }
  hover->visible = false;
  hover->click_triggered = false;
  hover->line = -1;
  hover->col = -1;
  hover->fetch_key.clear();
  hover->info = {};
}

void trigger_lsp_hover_at(EditorPanelState* panel, const CursorPos& pos, int anchor_x,
                          int anchor_y) {
  if (panel == nullptr || pos.line < 0) {
    return;
  }
  panel->hover.line = pos.line;
  panel->hover.col = pos.col;
  panel->hover.anchor_x = anchor_x;
  panel->hover.anchor_y = anchor_y;
  panel->hover.dwell_start_ms = steady_now_ms();
  panel->hover.visible = false;
  panel->hover.click_triggered = true;
  panel->hover.fetch_key.clear();
  panel->hover.info = {};
}

void claim_editor_focus(FocusManagerState* focus, MainLayoutState* layout_state,
                        FocusRegion panel_focus) {
  if (focus != nullptr) {
    focus->region = panel_focus;
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::None;
    layout_state->focus_sync_needed = true;
    layout_state->right_panel_active_section = 0;
  }
}

bool editor_mouse_targets_code(const EditorPanelState& panel, int x, int y) {
  return panel.code_box.Contain(x, y) || panel.gutter_box.Contain(x, y);
}

void rebuild_breadcrumb_hits(const Box& box, const std::vector<BreadcrumbItem>& crumbs,
                             std::vector<BreadcrumbHit>* hits) {
  hits->clear();
  if (!box.Contain(box.x_min, box.y_min)) {
    return;
  }
  int x = box.x_min + 1;
  for (std::size_t i = 0; i < crumbs.size(); ++i) {
    const int width = static_cast<int>(crumbs[i].label.size());
    BreadcrumbHit hit;
    hit.x_min = x;
    hit.x_max = x + width;
    hit.line = crumbs[i].line;
    hits->push_back(hit);
    x += width;
    if (i + 1 < crumbs.size()) {
      x += 3;
    }
  }
}

void tick_lsp_buffer_sync(EditorPanelState* panel, WorkspaceModel* workspace,
                          const std::shared_ptr<ISymbolProvider>& symbols,
                          MainLayoutState* layout_state) {
  if (panel == nullptr || workspace == nullptr || symbols == nullptr || !panel->lsp_sync_pending) {
    return;
  }
  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->lsp_enabled) {
    panel->lsp_sync_pending = false;
    return;
  }
  if (!editor_content_settled(*panel)) {
    return;
  }
  workspace->ensure_buffer();
  const EditorBuffer& buffer = workspace->buffer;
  if (buffer.path.empty() || !is_indexed_source_path(buffer.path)) {
    panel->lsp_sync_pending = false;
    return;
  }
  symbols->on_document_changed(buffer.path, buffer_text(buffer));
  panel->lsp_sync_pending = false;
}

const std::vector<SymbolInfo>& cached_file_symbols(EditorPanelState* panel,
                                                   const std::string& path,
                                                   ISymbolProvider* symbols) {
  if (panel == nullptr) {
    static const std::vector<SymbolInfo> kEmpty;
    return kEmpty;
  }
  if (path != panel->cached_symbols_path) {
    panel->cached_symbols_path = path;
    panel->cached_symbols.clear();
    panel->symbols_fetch_pending =
        !path.empty() && symbols != nullptr && !is_tabular_path(path);
  }
  return panel->cached_symbols;
}

void ensure_file_symbols(EditorPanelState* panel, ISymbolProvider* symbols,
                         const std::string& path, const std::string& buffer_text) {
  if (panel == nullptr || symbols == nullptr || path.empty() || !panel->symbols_fetch_pending) {
    return;
  }
  if (is_tabular_path(path)) {
    panel->cached_symbols.clear();
    panel->symbols_fetch_pending = false;
    return;
  }
  const std::vector<SymbolInfo> fetched = symbols->symbols_for_file(path);
  if (fetched.empty()) {
    if (symbols->symbols_lsp_pending(path)) {
      return;
    }
    if (!buffer_text.empty() && is_indexed_source_path(path) &&
        !tree_sitter_service().document_ready(path, buffer_text)) {
      tree_sitter_service().prepare_document(path, buffer_text);
      return;
    }
  }
  panel->cached_symbols = fetched;
  panel->symbols_fetch_pending = false;
}

void rebuild_diagnostics_by_line(EditorPanelState* panel, const DocumentDiagnostics& doc,
                                 uint64_t revision) {
  if (panel == nullptr) {
    return;
  }
  if (panel->diagnostics_by_line_path == doc.path &&
      revision == panel->diagnostics_by_line_revision) {
    return;
  }
  panel->diagnostics_by_line_path = doc.path;
  panel->diagnostics_by_line_revision = revision;
  panel->diagnostics_by_line.clear();
  panel->diagnostic_suffix_by_line.clear();
  panel->diagnostic_suffix_code_width = 0;
  panel->diagnostic_suffix_view_token = 0;
  panel->diagnostic_suffix_revision = 0;
  for (const auto& item : doc.items) {
    panel->diagnostics_by_line[item.line].push_back(item);
  }
}

void rebuild_diagnostic_suffix_cache(EditorPanelState* panel, const EditorBuffer& buffer,
                                     int code_width, uint64_t revision,
                                     ISymbolProvider* symbols, int64_t last_edit_ms,
                                     MainLayoutState* layout_state) {
  if (panel == nullptr || code_width <= 0) {
    return;
  }
  // Los sufijos se reconstruyen desde diagnósticos ya cacheados, así que deben seguir
  // visibles en reposo (inhibido). Solo respetamos el debounce de edición, no la inhibición.
  (void)layout_state;
  if (!diagnostics_display_allowed(last_edit_ms, symbols, buffer.path, /*lsp_ui_allowed=*/true)) {
    panel->diagnostic_suffix_by_line.clear();
    return;
  }
  if (panel->diagnostic_suffix_code_width == code_width &&
      panel->diagnostic_suffix_view_token == buffer.view_token &&
      panel->diagnostic_suffix_revision == revision) {
    return;
  }
  panel->diagnostic_suffix_code_width = code_width;
  panel->diagnostic_suffix_view_token = buffer.view_token;
  panel->diagnostic_suffix_revision = revision;
  panel->diagnostic_suffix_by_line.clear();
  for (const auto& entry : panel->diagnostics_by_line) {
    const int line = entry.first;
    if (line < 0 || line >= static_cast<int>(buffer.lines.size())) {
      continue;
    }
    const int max_suffix =
        code_width - static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size()) - 2;
    const std::string suffix = build_diagnostic_suffix(entry.second, max_suffix);
    if (!suffix.empty()) {
      panel->diagnostic_suffix_by_line[line] = suffix;
    }
  }
}

bool scroll_suffixes_settled(const EditorPanelState& panel) {
  return steady_now_ms() - panel.last_scroll_change_ms >= kSuffixScrollSettleMs;
}

bool show_diagnostic_suffix_on_line(const EditorPanelState& panel, int line,
                                    const EditorBuffer& buffer, bool suffixes_enabled) {
  if (!suffixes_enabled) {
    return false;
  }
  if (panel.diagnostic_suffix_by_line.find(line) == panel.diagnostic_suffix_by_line.end()) {
    return false;
  }
  if (scroll_suffixes_settled(panel)) {
    return true;
  }
  return line == buffer.primary_line();
}

void track_editor_scroll(EditorPanelState* panel, int scroll, MainLayoutState* layout_state) {
  if (panel == nullptr) {
    return;
  }
  if (scroll == panel->last_render_scroll) {
    return;
  }
  panel->last_render_scroll = scroll;
  panel->last_scroll_change_ms = steady_now_ms();
  if (layout_state != nullptr && layout_state->schedule_ui_tick) {
    layout_state->schedule_ui_tick();
  }
}

const std::vector<Diagnostic>* diagnostics_for_editor_line(EditorPanelState* panel, int line) {
  if (panel == nullptr) {
    return nullptr;
  }
  const auto it = panel->diagnostics_by_line.find(line);
  if (it == panel->diagnostics_by_line.end() || it->second.empty()) {
    return nullptr;
  }
  return &it->second;
}

char line_diagnostic_marker_from_map(EditorPanelState* panel, int line) {
  const std::vector<Diagnostic>* items = diagnostics_for_editor_line(panel, line);
  if (items == nullptr || items->empty()) {
    return '\0';
  }
  bool warning = false;
  for (const auto& item : *items) {
    if (item.severity == DiagnosticSeverity::kError) {
      return '!';
    }
    if (item.severity == DiagnosticSeverity::kWarning) {
      warning = true;
    }
  }
  return warning ? 'W' : '\0';
}

bool git_line_changed(EditorPanelState* panel, int line) {
  if (panel == nullptr) {
    return false;
  }
  if (panel->git_untracked_all_lines) {
    return line >= 0 && line < panel->git_cached_line_count;
  }
  return panel->git_changed_lines.count(line) > 0;
}

char line_gutter_marker(EditorPanelState* panel, int line) {
  const char diag = line_diagnostic_marker_from_map(panel, line);
  if (diag == '!') {
    return '!';
  }
  if (git_line_changed(panel, line)) {
    return 'G';
  }
  if (diag == 'W') {
    return 'W';
  }
  return '\0';
}

void apply_git_diff_to_panel(EditorPanelState* panel, const GitFileDiff& diff,
                             const std::vector<std::string>& buffer_lines) {
  if (!diff.untracked && diff.head_lines.empty() && diff.line_changes.empty()) {
    return;
  }
  panel->git_changed_lines.clear();
  panel->git_previous_by_line.clear();
  panel->git_cached_line_count = static_cast<int>(buffer_lines.size());
  if (diff.untracked) {
    panel->git_untracked_all_lines = true;
    return;
  }
  panel->git_untracked_all_lines = false;
  if (!diff.head_lines.empty()) {
    const LineDiffResult result = compute_line_diff(diff.head_lines, buffer_lines);
    panel->git_changed_lines = result.changed_new_lines;
    for (const auto& [line_no, content] : result.previous_content_by_new_line) {
      panel->git_previous_by_line[line_no] = content;
    }
    return;
  }
  if (!diff.line_changes.empty()) {
    for (const auto& [line_no, change] : diff.line_changes) {
      (void)change;
      panel->git_changed_lines.insert(line_no);
    }
    for (const auto& [line_no, content] : diff.previous_content_by_line) {
      panel->git_previous_by_line[line_no] = content;
    }
  }
}

void sync_git_cache(EditorPanelState* panel, GitService* git, const EditorBuffer& buffer) {
  if (panel == nullptr || git == nullptr || !git->is_repo() || buffer.path.empty()) {
    if (panel != nullptr) {
      panel->git_changed_lines.clear();
      panel->git_previous_by_line.clear();
      panel->git_untracked_all_lines = false;
      panel->git_cached_line_count = 0;
      panel->git_cache_path.clear();
      panel->git_cache_revision = 0;
    }
    return;
  }

  const uint64_t revision = git->cache_revision();
  if (buffer.path != panel->git_cache_path) {
    panel->git_cache_path = buffer.path;
    panel->git_cache_revision = 0;
    panel->git_cache_view_token = 0;
    panel->git_changed_lines.clear();
    panel->git_previous_by_line.clear();
    panel->git_untracked_all_lines = false;
    panel->git_cached_line_count = 0;
    git->refresh_file_diff(buffer.path);
    git->refresh_file_head(buffer.path);
  }

  if (revision != panel->git_cache_revision) {
    panel->git_cache_revision = revision;
    panel->git_cache_view_token = 0;
    if (!git->has_file_diff_text(buffer.path)) {
      git->refresh_file_diff(buffer.path);
    }
  }

  const GitFileDiff diff = git->file_diff(buffer.path);
  if (!diff.untracked && diff.head_lines.empty()) {
    git->refresh_file_head(buffer.path);
    if (!diff.loaded && diff.line_changes.empty()) {
      return;
    }
  } else if (!diff.loaded && !diff.untracked) {
    git->refresh_file_diff(buffer.path, true);
    git->refresh_file_head(buffer.path);
    return;
  }

  const int line_count = static_cast<int>(buffer.lines.size());
  if (panel->git_cache_path == buffer.path && panel->git_cache_revision == revision &&
      panel->git_cache_view_token == buffer.view_token &&
      panel->git_cached_line_count == line_count &&
      panel->git_untracked_all_lines == diff.untracked) {
    return;
  }

  apply_git_diff_to_panel(panel, diff, buffer.lines);
  panel->git_cache_view_token = buffer.view_token;
}

const BracketPairHighlight& cached_bracket_highlight(EditorPanelState* panel,
                                                   const EditorBuffer& buffer,
                                                   bool editor_focused) {
  static const BracketPairHighlight kEmpty{};
  if (panel == nullptr || !editor_focused) {
    return kEmpty;
  }
  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  if (!editor_content_settled(*panel) && panel->bracket_cache.valid) {
    return panel->bracket_cache;
  }
  if (panel->bracket_cache_token == buffer.view_token && panel->bracket_cache_line == line &&
      panel->bracket_cache_col == col) {
    return panel->bracket_cache;
  }
  panel->bracket_cache_token = buffer.view_token;
  panel->bracket_cache_line = line;
  panel->bracket_cache_col = col;
  panel->bracket_cache = find_bracket_pair_highlight(buffer, line, col);
  return panel->bracket_cache;
}

const BracketPairHighlight& cached_scope_bracket_highlight(EditorPanelState* panel,
                                                           const EditorBuffer& buffer,
                                                           bool editor_focused) {
  static const BracketPairHighlight kEmpty{};
  if (panel == nullptr || !editor_focused) {
    return kEmpty;
  }
  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  if (!editor_content_settled(*panel) && panel->scope_bracket_cache.valid) {
    return panel->scope_bracket_cache;
  }
  if (panel->scope_bracket_cache_token == buffer.view_token &&
      panel->scope_bracket_cache_line == line && panel->scope_bracket_cache_col == col) {
    return panel->scope_bracket_cache;
  }
  panel->scope_bracket_cache_token = buffer.view_token;
  panel->scope_bracket_cache_line = line;
  panel->scope_bracket_cache_col = col;
  panel->scope_bracket_cache = find_scope_bracket_pair(buffer, line, col);
  return panel->scope_bracket_cache;
}

const std::vector<ColoredBraceMarker>& cached_colored_braces(EditorPanelState* panel,
                                                             const EditorBuffer& buffer,
                                                             bool enabled) {
  static const std::vector<ColoredBraceMarker> kEmpty{};
  if (panel == nullptr || !enabled || buffer.path.empty() ||
      !is_indexed_source_path(buffer.path)) {
    return kEmpty;
  }
  if (!editor_content_settled(*panel)) {
    return panel->colored_brace_cache;
  }
  const bool path_changed = panel->colored_brace_cache_path != buffer.path;
  const bool token_changed = panel->colored_brace_cache_token != buffer.view_token;
  if (!path_changed && !token_changed) {
    return panel->colored_brace_cache;
  }
  const std::vector<ColoredBraceMarker> fresh = find_colored_curly_braces(buffer);
  if (!fresh.empty() || path_changed || panel->colored_brace_cache.empty()) {
    panel->colored_brace_cache = fresh;
    panel->colored_brace_cache_path = buffer.path;
    panel->colored_brace_cache_token = buffer.view_token;
  }
  return panel->colored_brace_cache;
}

SelectionOccurrenceKey selection_occurrence_key_from(const EditorBuffer& buffer) {
  SelectionOccurrenceKey key;
  key.path = buffer.path;
  if (buffer.cursors.size() == 1 && buffer.primary().has_selection()) {
    key.view_token = buffer.view_token;
    buffer.primary().normalized_range(&key.start_line, &key.start_col, &key.end_line, &key.end_col);
  }
  return key;
}

bool selection_occurrence_needle_is_identifier(const std::string& needle) {
  return !needle.empty() && is_ident_start(needle[0]) &&
         std::all_of(needle.begin(), needle.end(), [](unsigned char c) {
           return is_ident_char(static_cast<char>(c));
         });
}

void poll_selection_occurrence_matches(EditorPanelState* panel, const EditorBuffer& buffer,
                                     MainLayoutState* layout_state) {
  if (panel == nullptr || panel->selection_occurrence_inflight_id == 0) {
    return;
  }

  const SelectionOccurrenceKey current = selection_occurrence_key_from(buffer);
  std::vector<TextMatch> matches;
  if (!panel->selection_occurrence_runner.poll(panel->selection_occurrence_inflight_id, current,
                                             &matches)) {
    return;
  }

  panel->selection_occurrence_inflight_id = 0;
  panel->selection_occurrence_matches = std::move(matches);
  if (layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

void request_selection_occurrence_matches(EditorPanelState* panel, const EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }

  const SelectionOccurrenceKey key = selection_occurrence_key_from(buffer);
  if (key == panel->selection_occurrence_committed_key) {
    return;
  }

  panel->selection_occurrence_committed_key = key;
  panel->selection_occurrence_matches.clear();
  panel->selection_occurrence_runner.cancel();
  panel->selection_occurrence_inflight_id = 0;

  if (key.start_line < 0 || buffer.cursors.size() != 1 || !buffer.primary().has_selection() ||
      key.start_line != key.end_line) {
    return;
  }

  const std::string needle = selection_text(buffer, buffer.primary());
  constexpr std::size_t kMaxNeedle = 256;
  if (needle.size() < 2 || needle.size() > kMaxNeedle) {
    return;
  }
  if (std::all_of(needle.begin(), needle.end(),
                  [](unsigned char c) { return std::isspace(static_cast<unsigned char>(c)); })) {
    return;
  }

  const uint64_t request_id = ++panel->selection_occurrence_request_counter;
  panel->selection_occurrence_runner.start(request_id, key, buffer.lines, needle,
                                           selection_occurrence_needle_is_identifier(needle));
  panel->selection_occurrence_inflight_id = request_id;
}

void tick_selection_occurrence_matches(EditorPanelState* panel, const EditorBuffer& buffer,
                                       MainLayoutState* layout_state) {
  if (panel == nullptr) {
    return;
  }
  poll_selection_occurrence_matches(panel, buffer, layout_state);
  if (panel->mouse_selecting) {
    return;
  }
  request_selection_occurrence_matches(panel, buffer);
  if (panel->selection_occurrence_inflight_id != 0 && layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

const std::vector<TextMatch>* selection_occurrence_matches_for(EditorPanelState* panel,
                                                               const EditorBuffer& buffer,
                                                               bool find_bar_open) {
  (void)buffer;
  if (panel == nullptr || find_bar_open) {
    return nullptr;
  }
  if (panel->selection_occurrence_matches.empty()) {
    return nullptr;
  }
  return &panel->selection_occurrence_matches;
}

void sync_diagnostic_cache(EditorPanelState* panel, ISymbolProvider* symbols,
                           WorkspaceModel* workspace, WorkspaceIndexer* indexer,
                           MainLayoutState* layout_state) {
  if (panel == nullptr || symbols == nullptr || !symbols->supports_diagnostics()) {
    return;
  }
  const std::string path =
      workspace != nullptr && !workspace->buffer.path.empty() ? workspace->buffer.path
                                                                : std::string{};
  const int64_t last_edit_ms =
      workspace != nullptr ? workspace->last_buffer_edit_ms : panel->content_edit_ms;
  const bool lsp_ui_allowed =
      layout_state == nullptr || layout_state->activity_gate.allows_lsp_ui();
  if (!diagnostics_display_allowed(last_edit_ms, symbols, path, lsp_ui_allowed)) {
    return;
  }

  const uint64_t revision = symbols->diagnostics_revision();
  if (revision == panel->last_diag_revision && panel->last_diag_path == path) {
    return;
  }
  panel->last_diag_revision = revision;
  panel->last_diag_path = path;

  std::vector<std::string> workspace_files;
  if (indexer != nullptr) {
    const auto snapshot = indexer->snapshot();
    if (snapshot) {
      workspace_files = snapshot->files;
    }
  }

  const std::string workspace_root = workspace != nullptr ? workspace->root : std::string{};
  const std::string active_text =
      workspace != nullptr ? buffer_text(workspace->buffer) : std::string{};
  const auto docs = path.empty()
                        ? std::vector<DocumentDiagnostics>{}
                        : diagnostics_for_translation_unit(
                              symbols->workspace_diagnostics(), path, workspace_root,
                              workspace_files, active_text);
  count_workspace_diagnostics(docs, &panel->problem_errors, &panel->problem_warnings);
  panel->cached_file_diag = {};
  panel->cached_file_diag_revision = 0;
  if (!path.empty() && is_lsp_trackable_path(path)) {
    panel->cached_file_diag = symbols->diagnostics_for_file(path);
    panel->cached_file_diag_revision = revision;
  }
}

const DocumentDiagnostics& cached_file_diagnostics(EditorPanelState* panel,
                                                   ISymbolProvider* symbols,
                                                   const std::string& path,
                                                   int64_t last_edit_ms,
                                                   MainLayoutState* layout_state) {
  static const DocumentDiagnostics kEmpty;
  if (panel == nullptr || symbols == nullptr || !symbols->supports_diagnostics() ||
      path.empty() || !is_lsp_trackable_path(path)) {
    return kEmpty;
  }
  const bool lsp_ui_allowed =
      layout_state == nullptr || layout_state->activity_gate.allows_lsp_ui();
  const bool allow_refresh =
      diagnostics_display_allowed(last_edit_ms, symbols, path, lsp_ui_allowed);
  const uint64_t revision = symbols->diagnostics_revision();
  if (allow_refresh &&
      (revision != panel->cached_file_diag_revision || panel->cached_file_diag.path != path)) {
    panel->cached_file_diag = symbols->diagnostics_for_file(path);
    panel->cached_file_diag_revision = revision;
  }
  if (panel->cached_file_diag.path == path) {
    return panel->cached_file_diag;
  }
  return kEmpty;
}

void editor_hover_tick(WorkspaceModel* workspace, EditorPanelState* panel,
                       const std::shared_ptr<ISymbolProvider>& symbols,
                       MainLayoutState* layout_state) {
  if (!editor_lsp_hover_enabled()) {
    return;
  }
  if (workspace == nullptr || panel == nullptr) {
    return;
  }
  if (layout_state != nullptr) {
    if (!layout_state->activity_gate.allows_lsp_ui()) {
      return;
    }
    if (layout_state->app_settings != nullptr &&
        layout_state->app_settings->lsp_hover_on_click_only &&
        !panel->hover.click_triggered) {
      return;
    }
  }
  auto& hover = panel->hover;
  if (hover.line < 0 || hover.visible) {
    return;
  }
  const int64_t now_ms = steady_now_ms();
  if (!hover.click_triggered && now_ms - hover.dwell_start_ms < kHoverDelayMs) {
    return;
  }

  workspace->ensure_buffer();
  const EditorBuffer& buffer = workspace->buffer;
  if (buffer.path.empty() || symbols == nullptr || !symbols->supports_hover()) {
    return;
  }

  const std::string key = buffer.path + "|" + std::to_string(hover.line) + "|" +
                          std::to_string(hover.col) + "|" + std::to_string(buffer.view_token);
  if (hover.fetch_key == key) {
    if (hover.info.valid) {
      hover.visible = true;
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      return;
    }
    if (symbols->hover_uses_async_fetch()) {
      if (const auto polled = symbols->poll_hover(key)) {
        hover.info = *polled;
        hover.visible = polled->valid;
        if (hover.visible && layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      }
    }
    return;
  }

  if (!hover.fetch_key.empty() && symbols->hover_uses_async_fetch()) {
    if (const auto polled = symbols->poll_hover(hover.fetch_key)) {
      hover.info = *polled;
      hover.fetch_key = key;
      hover.visible = polled->valid;
      if (hover.visible && layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      return;
    }
    if (hover_key_same_position(hover.fetch_key, buffer.path, hover.line, hover.col)) {
      return;
    }
  }

  HoverParams params;
  params.path = buffer.path;
  params.text = buffer_text(buffer);
  params.line = hover.line;
  params.character = hover.col;
  if (symbols->hover_uses_async_fetch()) {
    symbols->request_hover(params, key);
    hover.fetch_key = key;
    return;
  }
  hover.info = symbols->hover_at(params);
  hover.fetch_key = key;
  hover.visible = hover.info.valid;
  if (hover.visible && layout_state != nullptr) {
    layout_state->request_ui_tick = true;
  }
}

void track_hover_mouse(EditorPanelState* panel, const Mouse& m, const EditorBuffer& buffer,
                       int visible_lines) {
  if (!editor_lsp_hover_enabled()) {
    return;
  }
  if (panel == nullptr) {
    return;
  }
  const CursorPos pos =
      mouse_to_cursor(m, *panel, buffer, visible_lines);
  if (pos.line == panel->hover.line && pos.col == panel->hover.col) {
    return;
  }
  panel->hover.line = pos.line;
  panel->hover.col = pos.col;
  panel->hover.anchor_x = m.x;
  panel->hover.anchor_y = m.y;
  panel->hover.dwell_start_ms = steady_now_ms();
  panel->hover.visible = false;
  panel->hover.fetch_key.clear();
  panel->hover.info = {};
}

bool handle_problems_button_click(MainLayoutState* layout_state, FocusManagerState* focus,
                                  EditorPanelState* panel, const Mouse& m) {
  if (layout_state == nullptr || panel == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed) {
    return false;
  }
  if (panel->problems_button_box.IsEmpty() || !panel->problems_button_box.Contain(m.x, m.y)) {
    return false;
  }
  trigger_press(layout_state, press_id::kEditorProblems);
  layout_state->console_visible = true;
  if (layout_state->console_tabs.selected_tab == ConsolePanelTabs::kProblems) {
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kTerminal;
  } else {
    layout_state->console_tabs.selected_tab = ConsolePanelTabs::kProblems;
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
  }
  layout_state->focus_sync_needed = true;
  layout_state->request_ui_tick = true;
  return true;
}

bool handle_breadcrumb_click(WorkspaceModel* workspace, FocusManagerState* focus,
                             MainLayoutState* layout_state, EditorPanelState* panel, const Mouse& m,
                             int visible_lines) {
  if (workspace == nullptr || panel == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed) {
    return false;
  }
  if (!panel->breadcrumb_box.Contain(m.x, m.y)) {
    return false;
  }
  if (!panel->problems_button_box.IsEmpty() && panel->problems_button_box.Contain(m.x, m.y)) {
    return false;
  }
  for (const auto& hit : panel->breadcrumb_hits) {
    if (m.x >= hit.x_min && m.x <= hit.x_max) {
      workspace->ensure_buffer();
      workspace->buffer.reset_to_single_cursor(hit.line, 0);
      workspace->buffer.scroll = std::max(0, hit.line - 2);
      workspace->buffer.view_token++;
      claim_editor_focus(focus, layout_state, panel->panel_focus);
      ensure_scroll_visible(&workspace->buffer, visible_lines, panel->code_width_chars);
      return true;
    }
  }
  return false;
}

void navigate_editor_to_line(WorkspaceModel* workspace, EditorPanelState* panel, int line,
                             int visible_lines) {
  if (workspace == nullptr || panel == nullptr) {
    return;
  }
  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  if (buffer->lines.empty()) {
    return;
  }
  line = std::max(0, std::min(line, static_cast<int>(buffer->lines.size()) - 1));
  buffer->reset_to_single_cursor(line, 0);
  buffer->scroll = std::max(0, line - visible_lines / 3);
  buffer->view_token++;
  clear_hover_state(&panel->hover);
  ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
}

bool tabular_view_ready(EditorPanelState* panel, const EditorBuffer& buffer);
bool scroll_tabular_lines(WorkspaceModel* workspace, EditorPanelState* panel, int delta_lines,
                            int visible_lines);
void scroll_tabular_columns(EditorBuffer* buffer, EditorPanelState* panel, int delta_columns,
                            int code_width);
int tabular_total_content_width(EditorPanelState* panel);
int tabular_max_scroll_col(EditorPanelState* panel, int code_width);
int editor_horizontal_content_width(const EditorBuffer& buffer, int code_width);
int editor_horizontal_max_scroll_col(const EditorBuffer& buffer, int code_width);
Element attach_horizontal_scrollbar(Element code, int total_content_width, int scroll_col,
                                    int code_width, bool hovered, bool active,
                                    EditorPanelState* panel);
int tabular_data_visible_lines(int visible_lines);
int tabular_max_allowed_scroll(EditorPanelState* panel, int visible_lines);
void clamp_tabular_scroll(EditorPanelState* panel, EditorBuffer* buffer, int visible_lines);
void maybe_request_tabular_chunk(EditorPanelState* panel, EditorBuffer* buffer, int visible_lines);

bool handle_overview_ruler_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                                 MainLayoutState* layout_state, EditorPanelState* panel,
                                 const Mouse& m, int visible_lines) {
  if (panel == nullptr || panel->overview_ruler_layout.bar_height <= 0) {
    return false;
  }

  const bool in_ruler = overview_ruler_contains(panel->overview_ruler_box, m.x, m.y);
  if (!in_ruler) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    if (scroll_tabular_lines(workspace, panel, -3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    if (scroll_tabular_lines(workspace, panel, 3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, 3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
  }

  claim_editor_focus(focus, layout_state, panel->panel_focus);
  const int local_y = m.y - panel->overview_ruler_box.y_min;
  const int line = overview_ruler_line_for_y(panel->overview_ruler_layout, local_y);
  navigate_editor_to_line(workspace, panel, line, visible_lines);
  return true;
}

bool apply_scrollbar_drag(WorkspaceModel* workspace, EditorPanelState* panel, int mouse_y,
                          int visible_lines) {
  if (workspace == nullptr || panel == nullptr || !panel->scrollbar_layout.scrollable) {
    return false;
  }
  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  const int local_y = mouse_y - panel->scrollbar_box.y_min;
  const int thumb_top = local_y - panel->scrollbar_drag_offset;
  int new_scroll = scroll_for_thumb_top(panel->scrollbar_layout, thumb_top);
  if (tabular_view_ready(panel, *buffer)) {
    new_scroll = std::min(new_scroll, tabular_max_allowed_scroll(panel, visible_lines));
  }
  if (buffer->scroll != new_scroll) {
    buffer->scroll = new_scroll;
    clear_hover_state(&panel->hover);
    if (tabular_view_ready(panel, *buffer)) {
      maybe_request_tabular_chunk(panel, buffer, visible_lines);
    }
  }
  return true;
}

bool handle_scrollbar_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                            MainLayoutState* layout_state, EditorPanelState* panel, const Mouse& m,
                            int visible_lines) {
  if (panel == nullptr || !panel->scrollbar_layout.scrollable) {
    return false;
  }

  const bool in_bar = panel->scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || panel->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorScrollbar);
      } else {
        layout_state->clickable.clear_hover_if([&](std::string_view id) {
          return id == press_id::kEditorScrollbar;
        });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (panel->scrollbar_dragging) {
      return apply_scrollbar_drag(workspace, panel, m.y, visible_lines);
    }
    return in_bar;
  }

  if (panel->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      panel->scrollbar_dragging = false;
      panel->tabular_scroll_locked = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      return apply_scrollbar_drag(workspace, panel, m.y, visible_lines);
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelUp) {
    if (scroll_tabular_lines(workspace, panel, -3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    if (scroll_tabular_lines(workspace, panel, 3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, 3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button != Mouse::Left) {
    return false;
  }

  if (m.motion == Mouse::Pressed) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    trigger_press(layout_state, press_id::kEditorScrollbar);
    const int local_y = m.y - panel->scrollbar_box.y_min;
    if (scrollbar_thumb_hit(panel->scrollbar_layout, panel->scrollbar_box, m.x, m.y)) {
      panel->scrollbar_dragging = true;
      panel->scrollbar_drag_offset = local_y - panel->scrollbar_layout.thumb_y;
    } else {
      const int thumb_top =
          local_y - panel->scrollbar_layout.thumb_height / 2;
      workspace->ensure_buffer();
      int new_scroll = scroll_for_thumb_top(panel->scrollbar_layout, thumb_top);
      if (tabular_view_ready(panel, workspace->buffer)) {
        new_scroll = std::min(new_scroll, tabular_max_allowed_scroll(panel, visible_lines));
      }
      workspace->buffer.scroll = new_scroll;
      if (tabular_view_ready(panel, workspace->buffer)) {
        maybe_request_tabular_chunk(panel, &workspace->buffer, visible_lines);
      }
      panel->scrollbar_dragging = true;
      panel->scrollbar_drag_offset = panel->scrollbar_layout.thumb_height / 2;
      clear_hover_state(&panel->hover);
    }
    return true;
  }

  if (m.motion == Mouse::Moved && panel->scrollbar_dragging) {
    return apply_scrollbar_drag(workspace, panel, m.y, visible_lines);
  }

  return false;
}

bool apply_h_scrollbar_drag(WorkspaceModel* workspace, EditorPanelState* panel, int mouse_x) {
  if (workspace == nullptr || panel == nullptr || !panel->h_scrollbar_layout.scrollable) {
    return false;
  }
  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  const int code_width = panel->code_width_chars;
  const int local_x = mouse_x - panel->h_scrollbar_box.x_min;
  const int thumb_left = local_x - panel->h_scrollbar_drag_offset;
  int new_scroll_col = scroll_for_thumb_left(panel->h_scrollbar_layout, thumb_left);
  if (tabular_view_ready(panel, *buffer)) {
    new_scroll_col = std::min(new_scroll_col, tabular_max_scroll_col(panel, code_width));
  } else {
    new_scroll_col = std::min(new_scroll_col, editor_horizontal_max_scroll_col(*buffer, code_width));
  }
  if (buffer->scroll_col != new_scroll_col) {
    buffer->scroll_col = new_scroll_col;
    clear_hover_state(&panel->hover);
  }
  return true;
}

bool handle_horizontal_scrollbar_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                                     MainLayoutState* layout_state, EditorPanelState* panel,
                                     const Mouse& m) {
  if (panel == nullptr || !panel->h_scrollbar_layout.scrollable) {
    return false;
  }

  const bool in_bar = panel->h_scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (layout_state != nullptr && hover_effects_enabled()) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || panel->h_scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorHorizontalScrollbar);
      } else {
        layout_state->clickable.clear_hover_if([&](std::string_view id) {
          return id == press_id::kEditorHorizontalScrollbar;
        });
      }
      apply_hover_repaint(layout_state, before);
    }
    if (panel->h_scrollbar_dragging) {
      return apply_h_scrollbar_drag(workspace, panel, m.x);
    }
    return in_bar;
  }

  if (panel->h_scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      panel->h_scrollbar_dragging = false;
      return true;
    }
    if (m.button == Mouse::Left && m.motion == Mouse::Moved) {
      return apply_h_scrollbar_drag(workspace, panel, m.x);
    }
  }

  if (!in_bar) {
    return false;
  }

  if (m.button == Mouse::WheelLeft || (m.shift && m.button == Mouse::WheelUp)) {
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    if (tabular_view_ready(panel, *buffer)) {
      scroll_tabular_columns(buffer, panel, -3, panel->code_width_chars);
    } else {
      scroll_view_by_columns(buffer, -3, panel->code_width_chars);
    }
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelRight || (m.shift && m.button == Mouse::WheelDown)) {
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    if (tabular_view_ready(panel, *buffer)) {
      scroll_tabular_columns(buffer, panel, 3, panel->code_width_chars);
    } else {
      scroll_view_by_columns(buffer, 3, panel->code_width_chars);
    }
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button != Mouse::Left) {
    return false;
  }

  if (m.motion == Mouse::Pressed) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    trigger_press(layout_state, press_id::kEditorHorizontalScrollbar);
    const int local_x = m.x - panel->h_scrollbar_box.x_min;
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    const int code_width = panel->code_width_chars;
    if (horizontal_scrollbar_thumb_hit(panel->h_scrollbar_layout, panel->h_scrollbar_box, m.x,
                                       m.y)) {
      panel->h_scrollbar_dragging = true;
      panel->h_scrollbar_drag_offset = local_x - panel->h_scrollbar_layout.thumb_x;
    } else {
      const int thumb_left = local_x - panel->h_scrollbar_layout.thumb_width / 2;
      int new_scroll_col = scroll_for_thumb_left(panel->h_scrollbar_layout, thumb_left);
      if (tabular_view_ready(panel, *buffer)) {
        new_scroll_col = std::min(new_scroll_col, tabular_max_scroll_col(panel, code_width));
      } else {
        new_scroll_col =
            std::min(new_scroll_col, editor_horizontal_max_scroll_col(*buffer, code_width));
      }
      buffer->scroll_col = new_scroll_col;
      panel->h_scrollbar_dragging = true;
      panel->h_scrollbar_drag_offset = panel->h_scrollbar_layout.thumb_width / 2;
      clear_hover_state(&panel->hover);
    }
    return true;
  }

  if (m.motion == Mouse::Moved && panel->h_scrollbar_dragging) {
    return apply_h_scrollbar_drag(workspace, panel, m.x);
  }

  return false;
}

bool handle_editor_chrome_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                                EditorPanelState* panel, EditorTabBarState* tab_bar,
                                MainLayoutState* layout_state, Event event, int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }
  const auto& m = event.mouse();
  if (m.motion == Mouse::Moved) {
    if (hover_effects_enabled()) {
      update_editor_chrome_hover(workspace, tab_bar, layout_state, panel->problems_button_box, m.x,
                                 m.y);
      if (tab_bar != nullptr && tab_bar->bar_box.Contain(m.x, m.y)) {
        return true;
      }
    }
  }
  if (handle_tab_bar_mouse(workspace, focus, tab_bar, m, layout_state, panel->panel_focus)) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  if (handle_problems_button_click(layout_state, focus, panel, m)) {
    return true;
  }
  if (handle_breadcrumb_click(workspace, focus, layout_state, panel, m, visible_lines)) {
    return true;
  }
  if (handle_overview_ruler_mouse(workspace, focus, layout_state, panel, m, visible_lines)) {
    return true;
  }
  if (handle_scrollbar_mouse(workspace, focus, layout_state, panel, m, visible_lines)) {
    return true;
  }
  if (handle_horizontal_scrollbar_mouse(workspace, focus, layout_state, panel, m)) {
    return true;
  }

  workspace->ensure_buffer();
  const bool in_code = panel->code_box.Contain(m.x, m.y);
  if (m.motion == Mouse::Moved) {
    if (!in_code && hover_effects_enabled() &&
        !panel->breadcrumb_box.Contain(m.x, m.y) &&
        (tab_bar == nullptr || !tab_bar->bar_box.Contain(m.x, m.y))) {
      clear_hover_state(&panel->hover);
    }
  }
  return false;
}

char line_diagnostic_marker(int line, const DocumentDiagnostics& doc) {
  bool warning = false;
  for (const auto& item : doc.items) {
    if (item.line != line) {
      continue;
    }
    if (item.severity == DiagnosticSeverity::kError) {
      return '!';
    }
    if (item.severity == DiagnosticSeverity::kWarning) {
      warning = true;
    }
  }
  return warning ? 'W' : '\0';
}

Color diagnostic_severity_color(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kError:
      return theme::Error();
    case DiagnosticSeverity::kWarning:
      return theme::Warning();
    case DiagnosticSeverity::kInfo:
      return theme::Accent();
    case DiagnosticSeverity::kHint:
    default:
      return theme::Muted();
  }
}

Element make_git_history_modal(const GitHistoryModalState& state) {
  if (!state.open) {
    return text("");
  }

  Elements rows;
  rows.push_back(text(i18n::tr_fmt("modal.git_history.line", {std::to_string(state.line + 1)})) |
                 color(theme::Success()) | bold);
  rows.push_back(separator() | color(theme::AccentDim()));
  rows.push_back(text(i18n::tr("modal.git_history.before")) | color(theme::Muted()) | bold);
  rows.push_back(paragraphAlignLeft(" " + (state.previous_content.empty() ? i18n::tr("common.empty")
                                                                          : state.previous_content)) |
                 color(theme::Header()));
  rows.push_back(text(""));
  rows.push_back(text(i18n::tr("modal.git_history.after")) | color(theme::Muted()) | bold);
  rows.push_back(paragraphAlignLeft(" " + (state.current_content.empty() ? i18n::tr("common.empty")
                                                                         : state.current_content)) |
                 color(theme::Header()));
  rows.push_back(text(""));
  rows.push_back(text(i18n::tr("common.footer.esc_close")) | color(theme::Muted()));

  Element dialog = ModalWindow(
      text(i18n::tr("modal.git_history.title")) | color(theme::Success()),
      vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, 40) | size(HEIGHT, LESS_THAN, 18));
  return CenteredModal(std::move(dialog));
}

int gutter_buffer_line_at_row(const EditorPanelState& panel, int row) {
  if (row >= 0 && row < static_cast<int>(panel.viewport_line_cache.size())) {
    return panel.viewport_line_cache[static_cast<std::size_t>(row)];
  }
  return panel.gutter_scroll_start + row;
}

bool handle_fold_gutter_click(EditorPanelState* panel, EditorBuffer* buffer, const Mouse& m,
                              int visible_lines) {
  if (panel == nullptr || buffer == nullptr || panel->gutter_fold_width <= 0 ||
      m.button != Mouse::Left || m.motion != Mouse::Pressed ||
      !panel->gutter_box.Contain(m.x, m.y)) {
    return false;
  }
  if (m.x - panel->gutter_box.x_min >= panel->gutter_fold_width) {
    return false;
  }
  const int row = m.y - panel->gutter_box.y_min;
  if (row < 0 || row >= static_cast<int>(panel->viewport_line_cache.size())) {
    return false;
  }
  const int line = panel->viewport_line_cache[static_cast<std::size_t>(row)];
  if (fold_gutter_marker(line, buffer->fold_regions, buffer->collapsed_folds) == '\0') {
    return false;
  }
  if (!toggle_fold_at(buffer, line, buffer->fold_regions)) {
    return false;
  }
  ensure_scroll_visible_fold_aware(buffer, buffer->fold_regions, visible_lines,
                                   panel->code_width_chars);
  return true;
}

bool handle_git_gutter_click(EditorPanelState* panel, GitHistoryModalState* modal,
                             GitService* git, const EditorBuffer& buffer, const Mouse& m) {
  if (panel == nullptr || modal == nullptr || git == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed || !panel->gutter_box.Contain(m.x, m.y)) {
    return false;
  }
  const int row = m.y - panel->gutter_box.y_min;
  if (row < 0 || row >= panel->gutter_visible_rows) {
    return false;
  }
  const int line = gutter_buffer_line_at_row(*panel, row);
  if (!git_line_changed(panel, line)) {
    return false;
  }
  modal->open = true;
  modal->line = line;
  modal->path = buffer.path;
  const auto prev_it = panel->git_previous_by_line.find(line);
  if (prev_it != panel->git_previous_by_line.end()) {
    modal->previous_content = prev_it->second;
  } else {
    modal->previous_content = git->previous_line_content(buffer.path, line);
  }
  if (line >= 0 && static_cast<std::size_t>(line) < buffer.lines.size()) {
    modal->current_content = buffer.lines[static_cast<std::size_t>(line)];
  } else {
    modal->current_content.clear();
  }
  return true;
}

bool handle_gutter_marker_click(EditorPanelState* panel, DiagnosticModalState* modal,
                                const DocumentDiagnostics& file_diag, const Mouse& m) {
  if (panel == nullptr || modal == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed || !panel->gutter_box.Contain(m.x, m.y)) {
    return false;
  }
  const int row = m.y - panel->gutter_box.y_min;
  if (row < 0 || row >= panel->gutter_visible_rows) {
    return false;
  }
  const int line = gutter_buffer_line_at_row(*panel, row);
  const char marker = line_gutter_marker(panel, line);
  if (marker == '\0' || marker == 'G') {
    return false;
  }
  modal->open = true;
  modal->line = line;
  modal->items = diagnostics_on_line(file_diag, line);
  return true;
}

std::string diagnostic_severity_tr(DiagnosticSeverity severity) {
  switch (severity) {
    case DiagnosticSeverity::kError:
      return i18n::tr("diagnostic.severity.error");
    case DiagnosticSeverity::kWarning:
      return i18n::tr("diagnostic.severity.warning");
    case DiagnosticSeverity::kInfo:
      return i18n::tr("diagnostic.severity.info");
    case DiagnosticSeverity::kHint:
    default:
      return i18n::tr("diagnostic.severity.hint");
  }
}

Element make_diagnostic_modal(const DiagnosticModalState& state) {
  if (!state.open || state.items.empty()) {
    return text("");
  }

  Elements rows;
  rows.push_back(text(i18n::tr_fmt("modal.diagnostic.line", {std::to_string(state.line + 1)})) |
                 color(theme::Accent()) | bold);
  rows.push_back(separator() | color(theme::AccentDim()));

  for (const auto& item : state.items) {
    std::string header = diagnostic_severity_tr(item.severity);
    if (!item.source.empty()) {
      header += i18n::tr_fmt("modal.diagnostic.source", {item.source});
    }
    if (item.start_col >= 0) {
      if (item.end_col > item.start_col) {
        header += i18n::tr_fmt("modal.diagnostic.col_range",
                               {std::to_string(item.start_col + 1), std::to_string(item.end_col)});
      } else {
        header += i18n::tr_fmt("modal.diagnostic.col", {std::to_string(item.start_col + 1)});
      }
    }
    rows.push_back(text(" " + header) | color(diagnostic_severity_color(item.severity)) | bold);
    rows.push_back(paragraphAlignLeft(" " + item.message) | color(theme::Header()));
    rows.push_back(text(""));
  }
  rows.push_back(text(i18n::tr("common.footer.esc_close")) | color(theme::Muted()));

  Element dialog = ModalWindow(
      text(i18n::tr("modal.diagnostic.title")) | color(theme::Accent()),
      vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, 40) | size(HEIGHT, LESS_THAN, 18));
  return CenteredModal(std::move(dialog));
}

Element make_breadcrumb_bar(const std::vector<BreadcrumbItem>& crumbs,
                            const EditorBuffer& buffer, const EditorFindState& find,
                            EditorPanelState* panel_state,
                            const std::shared_ptr<ISymbolProvider>& symbols,
                            MainLayoutState* layout_state) {
  Elements segments;
  for (std::size_t i = 0; i < crumbs.size(); ++i) {
    segments.push_back(text(crumbs[i].label) | color(theme::Accent()) | bold);
    if (i + 1 < crumbs.size()) {
      segments.push_back(text(i18n::tr("editor.breadcrumb.separator")) | color(theme::Muted()));
    }
  }

  std::string meta;
  meta += i18n::tr_fmt("editor.meta.line_col",
                       {std::to_string(buffer.primary_line() + 1),
                        std::to_string(buffer.primary_col() + 1)});
  if (buffer.dirty) {
    meta += i18n::tr("editor.meta.dirty");
  }
  if (buffer.multi_cursor_active()) {
    meta += i18n::tr_fmt("editor.meta.multi_cursor", {std::to_string(buffer.cursors.size())});
  }
  if (find.open) {
    meta += i18n::tr("editor.meta.find_open");
  }

  int problem_errors = 0;
  int problem_warnings = 0;
  if (panel_state != nullptr) {
    problem_errors = panel_state->problem_errors;
    problem_warnings = panel_state->problem_warnings;
  }
  std::string problems_label = i18n::tr("editor.problems.label");
  if (problem_errors > 0) {
    problems_label +=
        i18n::tr_fmt("editor.problems.count_errors", {std::to_string(problem_errors)});
  } else if (problem_warnings > 0) {
    problems_label +=
        i18n::tr_fmt("editor.problems.count_warnings", {std::to_string(problem_warnings)});
  }
  Color problems_color = theme::Muted();
  if (layout_state != nullptr && problems_tab_active(layout_state)) {
    problems_color = theme::Accent();
  } else if (problem_errors > 0) {
    problems_color = theme::Error();
  } else if (problem_warnings > 0) {
    problems_color = theme::Warning();
  }
  Element problems_btn = text(problems_label) | color(problems_color);
  const bool problems_hovered =
      layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kEditorProblems);
  const bool problems_pressed =
      layout_state != nullptr && layout_state->clickable.is_pressed(press_id::kEditorProblems);
  if (problems_pressed) {
    problems_btn = problems_btn | bold | inverted | bgcolor(theme::TabPressed());
  } else if (problems_hovered) {
    problems_btn = problems_btn | bold | bgcolor(theme::TabHover());
  } else if (layout_state != nullptr && problems_tab_active(layout_state)) {
    problems_btn = problems_btn | bgcolor(theme::TabActive());
  }
  problems_btn = problems_btn | reflect(panel_state->problems_button_box);

  Elements problems_row;
  const bool clangd_loading =
      layout_state != nullptr && layout_state->app_settings != nullptr &&
      layout_state->app_settings->lsp_enabled && symbols != nullptr && symbols->lsp_loading();
  if (clangd_loading) {
    problems_row.push_back(text(" " + spinner::glyph()) | color(theme::Muted()));
  }
  problems_row.push_back(problems_btn);

  Element bar = hbox({
                  text(" "),
                  hbox(std::move(segments)),
                  text(meta) | color(theme::Muted()),
                  hbox(std::move(problems_row)),
              }) |
              size(HEIGHT, EQUAL, 1) | bgcolor(theme::TabIdle()) |
              reflect(panel_state->breadcrumb_box);

  if (panel_state != nullptr) {
    panel_state->breadcrumbs = crumbs;
    rebuild_breadcrumb_hits(panel_state->breadcrumb_box, crumbs, &panel_state->breadcrumb_hits);
  }
  return bar;
}

Element make_hover_tooltip(const EditorHoverState& hover, const Box& code_box) {
  if (!hover.visible || !hover.info.valid) {
    return text("");
  }

  Elements rows;
  if (!hover.info.title.empty()) {
    rows.push_back(text(" " + hover.info.title) | bold | color(theme::Accent()) |
                   bgcolor(theme::PanelBg()));
  }
  for (const std::string& line : hover.info.body_lines) {
    rows.push_back(text(" " + line) | color(theme::Header()) | bgcolor(theme::PanelBg()));
  }
  if (rows.empty()) {
    return text("");
  }

  const int popup_rows = static_cast<int>(hover.info.body_lines.size() + (hover.info.title.empty() ? 0 : 1));
  Element popup = vbox(std::move(rows)) | border | bgcolor(theme::PanelBg());
  const int rel_x = std::max(0, hover.anchor_x - code_box.x_min + 1);
  const int rel_y = std::max(0, hover.anchor_y - code_box.y_min + 1);
  const int code_h = std::max(1, code_box.y_max - code_box.y_min + 1);
  const bool place_above = rel_y + popup_rows + 2 >= code_h;
  const int y_pad = place_above ? std::max(0, rel_y - popup_rows - 1) : rel_y + 1;

  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, rel_x), popup | clear_under, filler()}),
                     filler()}) |
                   flex});
}

Element make_sticky_overlay(const std::vector<StickyLine>& sticky_lines, int gutter_width,
                            const EditorBuffer& buffer,
                            const SemanticTokenDocument* semantic_tokens, int code_width,
                            bool indent_guides_enabled) {
  if (sticky_lines.empty()) {
    return text("");
  }
  SyntaxHighlightContext highlight_ctx;
  highlight_ctx.file_path = buffer.path;
  highlight_ctx.lines = &buffer.lines;
  highlight_ctx.buffer_token = buffer.view_token;
  Elements rows;
  for (const StickyLine& sticky : sticky_lines) {
    const std::string indent(static_cast<std::size_t>(sticky.depth * 2), ' ');
    const std::string& display_line =
        buffer.lines[static_cast<std::size_t>(sticky.source_line)];

    Element line = RenderEditorLine(
        display_line, sticky.source_line, buffer, false, nullptr, nullptr, semantic_tokens, nullptr,
        nullptr, 58, nullptr, nullptr, nullptr, nullptr, false, 0, code_width, &highlight_ctx, true,
        indent_guides_enabled,
        indent_guides_enabled
            ? indent_guide_depth_for_line(buffer.lines, sticky.source_line,
                                          std::max(1, editor_indent::width()))
            : 0);
    rows.push_back(hbox({text(indent) | bgcolor(theme::TabIdle()), line | flex}) | clear_under);
  }
  const int sticky_h = static_cast<int>(rows.size());
  return dbox({text(""),
               vbox({hbox({filler() | size(WIDTH, EQUAL, gutter_width + 1),
                           vbox(std::move(rows)) | bgcolor(theme::TabIdle()) | clear_under,
                           filler()}),
                     filler()}) |
                   flex | size(HEIGHT, EQUAL, sticky_h)});
}

bool is_same_click_spot(const EditorPanelState& panel, int line, int col, int64_t now_ms) {
  if (panel.last_click_line != line) {
    return false;
  }
  if (std::abs(panel.last_click_col - col) > 1) {
    return false;
  }
  return now_ms - panel.last_click_ms <= kDoubleClickMs;
}

bool is_double_click(const EditorPanelState& panel, int line, int col, int64_t now_ms) {
  return is_same_click_spot(panel, line, col, now_ms) && panel.last_click_count == 1;
}

bool is_triple_click(const EditorPanelState& panel, int line, int col, int64_t now_ms) {
  return is_same_click_spot(panel, line, col, now_ms) && panel.last_click_count >= 2;
}

bool sgr_mouse_button_field(const Event& event, int* button_out) {
  if (button_out != nullptr) {
    *button_out = 0;
  }
  if (!event.is_mouse()) {
    return false;
  }
  const std::string& input = event.input();
  if (input.size() < 6 || input[0] != '\x1b' || input[1] != '[' || input[2] != '<') {
    return false;
  }
  int button = 0;
  for (std::size_t i = 3; i < input.size() && input[i] != ';' && input[i] != 'M' && input[i] != 'm';
       ++i) {
    button = button * 10 + (input[i] - '0');
  }
  if (button_out != nullptr) {
    *button_out = button;
  }
  return true;
}

bool sgr_mouse_has_shift(const Event& event) {
  int button = 0;
  if (!sgr_mouse_button_field(event, &button)) {
    return false;
  }
  return (button & 4) != 0;
}

bool sgr_mouse_has_meta(const Event& event) {
  int button = 0;
  if (!sgr_mouse_button_field(event, &button)) {
    return false;
  }
  return (button & 8) != 0;
}

bool mouse_shift_active(const Mouse& m, const Event& event) {
  return m.shift || sgr_mouse_has_shift(event);
}

bool mouse_alt_active(const Mouse& m, const Event& event) {
  return m.meta || sgr_mouse_has_meta(event);
}

struct GotoLineState {
  bool open = false;
  std::string query;
};

struct CompletionMatch {
  CompletionItem item;
  std::string match_display;
  std::vector<std::size_t> match_indices;
};

std::string completion_anchor_key(const std::string& path, int replace_line,
                                  int replace_start) {
  if (path.empty()) {
    return {};
  }
  return path + "|" + std::to_string(replace_line) + "|" + std::to_string(replace_start);
}

bool is_member_access_at_cursor(const EditorBuffer& buffer) {
  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  if (line < 0 || line >= static_cast<int>(buffer.lines.size()) || col <= 0) {
    return false;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  if (col > static_cast<int>(text.size())) {
    return false;
  }
  const char prev = text[static_cast<std::size_t>(col - 1)];
  if (prev == '.') {
    return true;
  }
  return col >= 2 && text[static_cast<std::size_t>(col - 2)] == '-' && prev == '>';
}

int member_access_trigger_col(const EditorBuffer& buffer) {
  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  if (line < 0 || line >= static_cast<int>(buffer.lines.size()) || col <= 0) {
    return -1;
  }
  const std::string& text = buffer.lines[static_cast<std::size_t>(line)];
  if (col > static_cast<int>(text.size())) {
    return -1;
  }
  if (text[static_cast<std::size_t>(col - 1)] == '.') {
    return col - 1;
  }
  if (col >= 2 && text[static_cast<std::size_t>(col - 2)] == '-' &&
      text[static_cast<std::size_t>(col - 1)] == '>') {
    return col - 2;
  }
  return -1;
}

std::string completion_scope_key(const EditorBuffer& buffer) {
  if (buffer.path.empty()) {
    return {};
  }
  std::string key = buffer.path;
  const int line = buffer.primary_line();
  const std::vector<SymbolInfo> chain =
      tree_sitter_service().scope_chain_at(buffer.path, buffer.lines, line);
  if (chain.empty()) {
    key += "|noscope|L";
    key += std::to_string(line);
  } else {
    for (const SymbolInfo& sym : chain) {
      key += "|";
      key += std::to_string(static_cast<int>(sym.kind));
      key += ":";
      key += sym.name;
      key += "@";
      key += std::to_string(sym.line);
    }
  }
  if (is_member_access_at_cursor(buffer)) {
    const int trigger_col = member_access_trigger_col(buffer);
    key += "|member@";
    key += std::to_string(line);
    key += ":";
    key += std::to_string(trigger_col);
  } else {
    key += "|ident";
  }
  return key;
}

bool completion_starts_with(std::string_view haystack_lower, std::string_view query_lower) {
  return haystack_lower.size() >= query_lower.size() &&
         haystack_lower.compare(0, query_lower.size(), query_lower) == 0;
}

std::vector<std::size_t> completion_prefix_indices(std::size_t length) {
  std::vector<std::size_t> indices;
  indices.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    indices.push_back(i);
  }
  return indices;
}

struct CompletionCacheEntry {
  CompletionItem item;
  std::string match_text;
  std::string match_lower;
  std::string filter_text;
  std::string filter_lower;
  std::string alt_text;
  std::string alt_lower;
  std::string sort_key;
  int source_index = 0;
  // 0 = fresh LSP, 1 = scope LSP cache, 2 = tree-sitter / sync fallback, 3 = snippets
  int source_priority = 0;
};

struct CompletionRankedCandidate {
  std::size_t index = 0;
  int tier = 1;  // 0 = prefix match, 1 = fuzzy only
  int score = 0;
  std::string match_display;
  std::vector<std::size_t> match_indices;
};

std::optional<CompletionRankedCandidate> rank_completion_entry(
    std::size_t cache_index, const CompletionCacheEntry& entry, std::string_view query_lower) {
  struct Attempt {
    int tier = 1;
    int score = 0;
    std::string match_display;
    std::vector<std::size_t> match_indices;
  };
  std::optional<Attempt> best;

  const auto consider = [&](std::string_view display, std::string_view lower) {
    if (display.empty() || lower.size() != display.size()) {
      return;
    }
    if (completion_starts_with(lower, query_lower)) {
      Attempt attempt;
      attempt.tier = entry.source_priority * 2;
      attempt.score = 100000 - static_cast<int>(display.size());
      attempt.match_display.assign(display);
      attempt.match_indices = completion_prefix_indices(query_lower.size());
      if (!best || best->tier > 0 || attempt.score > best->score) {
        best = std::move(attempt);
      }
      return;
    }
    if (best && best->tier == entry.source_priority * 2) {
      return;
    }
    const FuzzyMatchResult fuzzy = fuzzy_match_cached(display, lower, query_lower);
    if (!fuzzy.matched) {
      return;
    }
    Attempt attempt;
    attempt.tier = entry.source_priority * 2 + 1;
    attempt.score = fuzzy.score;
    attempt.match_display.assign(display);
    attempt.match_indices = fuzzy.indices;
    if (!best || best->tier > 1 || attempt.score > best->score) {
      best = std::move(attempt);
    }
  };

  if (!entry.filter_lower.empty()) {
    consider(entry.filter_text, entry.filter_lower);
  }
  consider(entry.match_text, entry.match_lower);
  if (!entry.alt_lower.empty()) {
    consider(entry.alt_text, entry.alt_lower);
  }
  if (!best) {
    return std::nullopt;
  }

  CompletionRankedCandidate ranked;
  ranked.index = cache_index;
  ranked.tier = best->tier;
  ranked.score = best->score;
  ranked.match_display = std::move(best->match_display);
  ranked.match_indices = std::move(best->match_indices);
  return ranked;
}

bool completion_ranked_before(const CompletionRankedCandidate& a,
                              const CompletionRankedCandidate& b,
                              const std::vector<CompletionCacheEntry>& cache) {
  if (a.tier != b.tier) {
    return a.tier < b.tier;
  }
  if (a.score != b.score) {
    return a.score > b.score;
  }
  const std::string& sort_a = cache[a.index].sort_key;
  const std::string& sort_b = cache[b.index].sort_key;
  if (sort_a != sort_b) {
    return sort_a < sort_b;
  }
  return cache[a.index].source_index < cache[b.index].source_index;
}

struct CompletionState {
  bool open = false;
  bool live_mode = false;
  bool semantic_mode = false;
  bool item_cache_dirty = true;
  std::string prefix;
  std::string query;
  std::string query_lower;
  std::string lsp_fetch_key;
  std::string lsp_pending_key;
  std::string lsp_inflight_key;
  std::string lsp_resolved_key;
  int64_t lsp_request_due_ms = 0;
  int64_t lsp_last_request_ms = 0;
  std::string lsp_resolved_query;
  std::vector<CompletionItem> lsp_items;
  std::vector<CompletionItem> snippet_items;
  std::vector<CompletionItem> all_items;
  std::vector<CompletionCacheEntry> item_cache;
  std::vector<CompletionMatch> matches;
  int selected = 0;
  int replace_line = 0;
  int replace_start = 0;
  int replace_end = 0;
  int lsp_fetch_line = 0;
  int lsp_fetch_col = 0;
  std::string active_scope_key;
  std::vector<CompletionItem> scope_cached_items;
  std::vector<CompletionItem> ts_fallback_items;

  void invalidate_item_cache() { item_cache_dirty = true; }

  void sync_scope_cache(const EditorBuffer& buffer, EditorPanelState* panel) {
    const std::string key = completion_scope_key(buffer);
    if (key.empty()) {
      return;
    }
    if (key == active_scope_key) {
      return;
    }
    active_scope_key = key;
    scope_cached_items.clear();
    if (panel != nullptr && panel->scope_completion_cache_key == key &&
        !panel->scope_completion_cache_items.empty()) {
      scope_cached_items = panel->scope_completion_cache_items;
    }
  }

  void commit_scope_cache(const EditorBuffer& buffer, EditorPanelState* panel,
                          const std::vector<CompletionItem>& items) {
    if (items.empty()) {
      return;
    }
    const std::string key = completion_scope_key(buffer);
    if (key.empty()) {
      return;
    }
    active_scope_key = key;
    scope_cached_items = items;
    if (panel != nullptr) {
      panel->scope_completion_cache_key = key;
      panel->scope_completion_cache_items = items;
    }
  }

  bool apply_polled_lsp_response(const std::string& key, std::vector<CompletionItem> items,
                                 const EditorBuffer& buffer, EditorPanelState* panel) {
    if (key != lsp_pending_key) {
      return false;
    }
    lsp_inflight_key.clear();
    lsp_items = std::move(items);
    lsp_resolved_query = query;
    lsp_resolved_key = key;
    if (!lsp_items.empty()) {
      commit_scope_cache(buffer, panel, lsp_items);
      semantic_mode = true;
    } else {
      const std::string scope_key = completion_scope_key(buffer);
      active_scope_key = scope_key;
      scope_cached_items.clear();
      if (panel != nullptr && panel->scope_completion_cache_key == scope_key) {
        panel->scope_completion_cache_items.clear();
      }
      semantic_mode = false;
    }
    refresh_ts_fallback(buffer);
    invalidate_item_cache();
    return true;
  }

  void refresh_ts_fallback(const EditorBuffer& buffer) {
    ts_fallback_items.clear();
    if (!lsp_items.empty() || buffer.path.empty() || !is_indexed_source_path(buffer.path)) {
      return;
    }
    CompletionParams params;
    params.path = buffer.path;
    params.text = buffer_text(buffer);
    params.line = buffer.primary_line();
    params.character = buffer.primary_col();
    ts_fallback_items = tree_sitter_service().local_completions_at(params);
  }

  bool has_fallback_items() const {
    return !scope_cached_items.empty() || !ts_fallback_items.empty() || !all_items.empty();
  }

  void rebuild_item_cache() {
    item_cache.clear();
    int source_index = 0;
    const auto add_item = [this, &source_index](const CompletionItem& item, int priority) {
      CompletionCacheEntry entry;
      entry.item = item;
      entry.source_priority = priority;
      entry.match_text = strip_symbol_kind_prefix(item.label);
      entry.match_lower = fuzzy_to_lower(entry.match_text);
      if (!item.filter_text.empty()) {
        entry.filter_text = item.filter_text;
        entry.filter_lower = fuzzy_to_lower(entry.filter_text);
      }
      if (!item.insert_text.empty()) {
        const std::string insert_name = symbol_insert_name(item.insert_text);
        if (insert_name != entry.match_text) {
          entry.alt_text = insert_name;
          entry.alt_lower = fuzzy_to_lower(entry.alt_text);
        }
      }
      entry.sort_key = !item.sort_text.empty() ? item.sort_text : entry.match_text;
      entry.source_index = source_index++;
      item_cache.push_back(std::move(entry));
    };
    const auto add_items = [&](const std::vector<CompletionItem>& items, int priority) {
      for (const CompletionItem& item : items) {
        add_item(item, priority);
      }
    };

    if (!lsp_items.empty()) {
      add_items(lsp_items, 0);
    } else {
      if (!scope_cached_items.empty()) {
        add_items(scope_cached_items, 1);
      }
      if (!ts_fallback_items.empty()) {
        add_items(ts_fallback_items, 2);
      } else if (!all_items.empty()) {
        add_items(all_items, 2);
      }
    }
    add_items(snippet_items, 3);
    item_cache_dirty = false;
  }

  void refresh_matches() {
    if (item_cache_dirty) {
      rebuild_item_cache();
    }
    query_lower = fuzzy_to_lower(query);

    std::vector<CompletionRankedCandidate> candidates;
    candidates.reserve(std::min(item_cache.size(), std::size_t{256}));

    if (query_lower.empty()) {
      std::vector<std::size_t> order(item_cache.size());
      for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
      }
      std::sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        const CompletionCacheEntry& ea = item_cache[a];
        const CompletionCacheEntry& eb = item_cache[b];
        if (ea.source_priority != eb.source_priority) {
          return ea.source_priority < eb.source_priority;
        }
        if (ea.sort_key != eb.sort_key) {
          return ea.sort_key < eb.sort_key;
        }
        return ea.source_index < eb.source_index;
      });

      constexpr int kMaxMatches = 200;
      const std::size_t limit = std::min(order.size(), std::size_t{kMaxMatches});
      matches.clear();
      matches.reserve(limit);
      for (std::size_t i = 0; i < limit; ++i) {
        const CompletionCacheEntry& entry = item_cache[order[i]];
        matches.push_back({entry.item, entry.match_text, {}});
      }
    } else {
      for (std::size_t i = 0; i < item_cache.size(); ++i) {
        const CompletionCacheEntry& entry = item_cache[i];
        if (const std::optional<CompletionRankedCandidate> ranked =
                rank_completion_entry(i, entry, query_lower)) {
          candidates.push_back(*ranked);
        }
      }

      std::sort(candidates.begin(), candidates.end(),
                [this](const CompletionRankedCandidate& a, const CompletionRankedCandidate& b) {
                  return completion_ranked_before(a, b, item_cache);
                });

      constexpr int kMaxMatches = 200;
      if (static_cast<int>(candidates.size()) > kMaxMatches) {
        candidates.resize(static_cast<std::size_t>(kMaxMatches));
      }

      matches.clear();
      matches.reserve(candidates.size());
      for (const CompletionRankedCandidate& candidate : candidates) {
        const CompletionCacheEntry& entry = item_cache[candidate.index];
        matches.push_back(
            {entry.item, candidate.match_display, candidate.match_indices});
      }
    }

    const bool has_semantic_source = !lsp_items.empty() || !scope_cached_items.empty() ||
                                     !ts_fallback_items.empty() || !all_items.empty();
    semantic_mode = has_semantic_source || !snippet_items.empty();
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
  }

  void sync_symbols(WorkspaceModel* workspace,
                    const std::shared_ptr<ISymbolProvider>& symbols,
                    SymbolWorkspaceIndexer* symbol_indexer, bool allow_lsp = false) {
    (void)symbol_indexer;
    TGDB_MON_SCOPE("editor", "completion.sync_symbols");
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    workspace->buffer.ensure_cursors();
    const int line = workspace->buffer.primary_line();
    const int col = workspace->buffer.primary_col();

    semantic_mode = false;

    if (!allow_lsp || symbols == nullptr || !symbols->supports_semantic_completion() ||
        path.empty()) {
      if (!path.empty() && is_indexed_source_path(path)) {
        snippet_items = structure_snippet_completions("");
      } else {
        snippet_items.clear();
      }
      invalidate_item_cache();
      refresh_matches();
      return;
    }

    const std::string lsp_key = completion_anchor_key(path, replace_line, replace_start);
    std::vector<CompletionItem> lsp_items;
    if (lsp_fetch_key != lsp_key) {
      lsp_fetch_key = lsp_key;
      symbols->flush_document_sync(path);
      CompletionParams params;
      params.path = path;
      params.text = buffer_text(workspace->buffer);
      params.line = line;
      params.character = col;
      lsp_items = symbols->completions_at(params);
      semantic_mode = !lsp_items.empty();
      invalidate_item_cache();
    }

    if (!path.empty() && is_indexed_source_path(path)) {
      snippet_items = structure_snippet_completions("");
      if (!lsp_items.empty()) {
        all_items = std::move(lsp_items);
      } else {
        CompletionParams params;
        params.path = path;
        params.text = buffer_text(workspace->buffer);
        params.line = line;
        params.character = col;
        all_items = tree_sitter_service().local_completions_at(params);
        if (!all_items.empty()) {
          semantic_mode = true;
        }
      }
      ts_fallback_items = all_items;
    } else {
      snippet_items.clear();
      all_items = std::move(lsp_items);
    }
    invalidate_item_cache();

    refresh_matches();
  }

  void close(MainLayoutState* layout_state) {
    open = false;
    live_mode = false;
    prefix.clear();
    query.clear();
    query_lower.clear();
    lsp_fetch_key.clear();
    lsp_pending_key.clear();
    lsp_inflight_key.clear();
    lsp_resolved_key.clear();
    lsp_request_due_ms = 0;
    lsp_last_request_ms = 0;
    lsp_resolved_query.clear();
    lsp_items.clear();
    scope_cached_items.clear();
    ts_fallback_items.clear();
    active_scope_key.clear();
    snippet_items.clear();
    all_items.clear();
    item_cache.clear();
    item_cache_dirty = true;
    matches.clear();
    semantic_mode = false;
    selected = 0;
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
  }
};

int visible_line_count(const Box& box) {
  if (box.y_max < box.y_min) {
    return 1;
  }
  return std::max(1, box.y_max - box.y_min + 1);
}

int max_scroll(int total, int visible) { return std::max(0, total - visible); }

int code_width_from_box(const Box& box, int fallback) {
  if (box.x_max > box.x_min) {
    return box.x_max - box.x_min + 1;
  }
  return std::max(1, fallback);
}

int tabular_data_visible_lines(int visible_lines) {
  return std::max(1, visible_lines - 1);
}

int tabular_max_allowed_scroll(EditorPanelState* panel, int visible_lines) {
  if (panel == nullptr || panel->tabular_store == nullptr || !panel->tabular_store->ready()) {
    return 0;
  }
  const int data_total = panel->tabular_store->max_data_total();
  const int data_visible = tabular_data_visible_lines(visible_lines);
  int allowed = max_scroll(data_total, data_visible);
  if (panel->tabular_scroll_locked || panel->tabular_store->loading_more()) {
    allowed = std::min(allowed, panel->tabular_scroll_limit);
  }
  return allowed;
}

void clamp_tabular_scroll(EditorPanelState* panel, EditorBuffer* buffer, int visible_lines) {
  if (panel == nullptr || buffer == nullptr || panel->tabular_store == nullptr ||
      !panel->tabular_store->ready()) {
    return;
  }
  const int allowed = tabular_max_allowed_scroll(panel, visible_lines);
  buffer->scroll = std::max(0, std::min(buffer->scroll, allowed));
}

void maybe_request_tabular_chunk(EditorPanelState* panel, EditorBuffer* buffer, int visible_lines) {
  if (panel == nullptr || buffer == nullptr || panel->tabular_store == nullptr ||
      !panel->tabular_store->ready()) {
    return;
  }
  TabularFileStore* store = panel->tabular_store.get();
  if (!store->has_more() || store->loading_more() || panel->tabular_scroll_locked) {
    return;
  }
  const int data_visible = tabular_data_visible_lines(visible_lines);
  const int data_total = store->max_data_total();
  if (buffer->scroll < max_scroll(data_total, data_visible)) {
    return;
  }
  if (store->request_load_at_end(buffer->scroll, data_visible)) {
    panel->tabular_scroll_locked = true;
    panel->tabular_scroll_limit = buffer->scroll;
  }
}

bool tabular_view_ready(EditorPanelState* panel, const EditorBuffer& buffer) {
  return is_tabular_path(buffer.path) && panel != nullptr && panel->tabular_store != nullptr &&
         panel->tabular_store->ready();
}

bool scroll_tabular_lines(WorkspaceModel* workspace, EditorPanelState* panel, int delta_lines,
                          int visible_lines) {
  if (workspace == nullptr || !tabular_view_ready(panel, workspace->buffer)) {
    return false;
  }
  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  const int data_total = panel->tabular_store->max_data_total();
  const int data_visible = tabular_data_visible_lines(visible_lines);
  buffer->scroll =
      std::max(0, std::min(buffer->scroll + delta_lines, max_scroll(data_total, data_visible)));
  clamp_tabular_scroll(panel, buffer, visible_lines);
  maybe_request_tabular_chunk(panel, buffer, visible_lines);
  buffer->view_token++;
  return true;
}

int tabular_total_content_width(EditorPanelState* panel) {
  if (panel == nullptr || panel->tabular_store == nullptr || !panel->tabular_store->ready()) {
    return 0;
  }
  const TabularTableLayout& layout = panel->tabular_store->layout();
  int total_width = tabular_row_width(layout);
  const std::string header_line = panel->tabular_store->row_at(0);
  if (!header_line.empty()) {
    const auto header_cells =
        parse_tabular_row(header_line, panel->tabular_store->delimiter());
    total_width = std::max(total_width, tabular_row_width_for_cells(header_cells, layout));
  }
  return total_width;
}

int tabular_max_scroll_col(EditorPanelState* panel, int code_width) {
  return std::max(0, tabular_total_content_width(panel) - std::max(1, code_width));
}

void clamp_tabular_scroll_col(EditorBuffer* buffer, EditorPanelState* panel, int code_width) {
  if (buffer == nullptr || panel == nullptr) {
    return;
  }
  buffer->scroll_col = std::min(buffer->scroll_col, tabular_max_scroll_col(panel, code_width));
}

void scroll_tabular_columns(EditorBuffer* buffer, EditorPanelState* panel, int delta_columns,
                            int code_width) {
  if (buffer == nullptr || panel == nullptr || panel->tabular_store == nullptr ||
      !panel->tabular_store->ready()) {
    return;
  }
  const int max_scroll_col = tabular_max_scroll_col(panel, code_width);
  buffer->scroll_col =
      std::max(0, std::min(buffer->scroll_col + delta_columns, max_scroll_col));
  buffer->view_token++;
}

int editor_horizontal_content_width(const EditorBuffer& buffer, int code_width) {
  const int max_len = editor_buffer_max_line_length(buffer);
  const int max_scroll_col = std::max(0, max_len - code_width + 1);
  if (max_scroll_col <= 0) {
    return code_width;
  }
  return max_scroll_col + code_width;
}

int editor_horizontal_max_scroll_col(const EditorBuffer& buffer, int code_width) {
  const int max_len = editor_buffer_max_line_length(buffer);
  return std::max(0, max_len - code_width + 1);
}

Element attach_horizontal_scrollbar(Element code, int total_content_width, int scroll_col,
                                    int code_width, bool hovered, bool active,
                                    EditorPanelState* panel) {
  panel->h_scrollbar_layout = compute_horizontal_scrollbar_layout(total_content_width, scroll_col,
                                                                 code_width, code_width);
  if (!panel->h_scrollbar_layout.scrollable) {
    panel->h_scrollbar_layout = {};
    return code | flex;
  }
  Element h_bar =
      horizontal_scrollbar(total_content_width, scroll_col, code_width, code_width, hovered, active);
  Element h_track = std::move(h_bar) | reflect(panel->h_scrollbar_box) | bgcolor(theme::CodeBg());
  return vbox({std::move(code) | flex, std::move(h_track) | size(HEIGHT, EQUAL, 1)}) | flex;
}

bool is_ident_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool completion_allowed_at_cursor(EditorBuffer& buffer) {
  buffer.ensure_cursors();
  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  return cursor_in_code(buffer, line, col);
}

NavigationParams navigation_params_for_buffer(WorkspaceModel* workspace) {
  NavigationParams params;
  if (workspace == nullptr) {
    return params;
  }
  workspace->buffer.ensure_cursors();
  params.path = workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  params.text = buffer_text(workspace->buffer);
  params.line = workspace->buffer.primary_line();
  params.character = workspace->buffer.primary_col();
  return params;
}

NavigationParams navigation_params_at(WorkspaceModel* workspace, int line, int col) {
  NavigationParams params = navigation_params_for_buffer(workspace);
  params.line = line;
  params.character = col;
  return params;
}

bool navigate_to_location(WorkspaceModel* workspace, MainLayoutState* layout_state,
                          const SourceLocation& loc, int visible_lines) {
  if (workspace == nullptr || !loc.valid || loc.path.empty()) {
    return false;
  }
  workspace->record_cursor_jump();
  workspace->open_file_at(loc.path, loc.line, loc.character);
  workspace->status_message =
      "→ " + std::filesystem::path(loc.path).filename().string() + ":" +
      std::to_string(loc.line + 1) + ":" + std::to_string(loc.character + 1);
  ensure_scroll_visible(&workspace->buffer, visible_lines);
  return true;
}

void flash_symbol_at_cursor(WorkspaceModel* workspace, MainLayoutState* layout_state,
                            EditorPanelState* panel_state, int line, int col, int visible_lines) {
  flash_symbol_at_buffer_pos_impl(workspace, layout_state, panel_state, line, col, visible_lines);
}

bool try_go_to_symbol(WorkspaceModel* workspace, MainLayoutState* layout_state,
                      EditorPanelState* panel_state,
                      const std::shared_ptr<ISymbolProvider>& symbols, int line, int col,
                      bool declaration, int visible_lines) {
  if (workspace == nullptr || symbols == nullptr || !symbols->supports_navigation()) {
    return false;
  }
  const NavigationParams params = navigation_params_at(workspace, line, col);
  if (params.path.empty()) {
    return false;
  }
  flash_symbol_at_cursor(workspace, layout_state, panel_state, line, col, visible_lines);
  SourceLocation loc = resolve_symbol_navigation(*symbols, params, declaration);
  if (!loc.valid) {
    workspace->status_message =
        declaration ? i18n::tr("status.no_declaration") : i18n::tr("status.no_definition");
    return false;
  }
  schedule_editor_navigation(layout_state, loc);
  return true;
}

void prepare_completion_at_cursor(CompletionState* completion, EditorBuffer* buffer,
                                  EditorPanelState* panel = nullptr) {
  if (completion == nullptr || buffer == nullptr) {
    return;
  }
  completion->sync_scope_cache(*buffer, panel);
  completion->prefix = completion_prefix_at_cursor(*buffer, buffer->primary());
  completion->query = completion->prefix;
  completion->selected = 0;
  completion->replace_line = buffer->primary().head.line;
  completion_replace_range_at_cursor(*buffer, buffer->primary(), &completion->replace_start,
                                     &completion->replace_end);
}

bool completion_uses_async_lsp(MainLayoutState* layout_state,
                               const std::shared_ptr<ISymbolProvider>& symbols,
                               const std::string& buffer_path) {
  if (layout_state == nullptr || layout_state->app_settings == nullptr) {
    return false;
  }
  if (!layout_state->app_settings->lsp_enabled) {
    return false;
  }
  if (buffer_path.empty() || !is_indexed_source_path(buffer_path)) {
    return false;
  }
  if (symbols == nullptr || !symbols->supports_semantic_completion() ||
      !symbols->completion_uses_async_fetch()) {
    return false;
  }
  return true;
}

bool live_completion_uses_async_lsp(MainLayoutState* layout_state,
                                  const std::shared_ptr<ISymbolProvider>& symbols,
                                  const std::string& buffer_path) {
  if (layout_state == nullptr || layout_state->app_settings == nullptr) {
    return false;
  }
  if (!layout_state->app_settings->lsp_enabled ||
      !layout_state->app_settings->live_lsp_completion_enabled) {
    return false;
  }
  if (buffer_path.empty() || !is_indexed_source_path(buffer_path)) {
    return false;
  }
  if (symbols == nullptr || !symbols->supports_semantic_completion() ||
      !symbols->completion_uses_async_fetch()) {
    return false;
  }
  return true;
}

bool completion_awaiting_async_lsp(const CompletionState* completion,
                                   MainLayoutState* layout_state,
                                   const std::shared_ptr<ISymbolProvider>& symbols,
                                   const std::string& buffer_path,
                                   const std::string& buffer_text) {
  if (completion == nullptr || !completion->open) {
    return false;
  }
  if (!completion_uses_async_lsp(layout_state, symbols, buffer_path)) {
    return false;
  }
  if (completion->lsp_pending_key.empty()) {
    return false;
  }
  if (completion->lsp_pending_key == completion->lsp_resolved_key) {
    return false;
  }
  const int64_t now_ms = steady_now_ms();
  const int64_t wait_ms =
      live_completion_lsp_wait_timeout_ms(buffer_text, completion->lsp_fetch_line);
  if (completion->lsp_request_due_ms > 0 &&
      now_ms > completion->lsp_request_due_ms + wait_ms) {
    return false;
  }
  if (!completion->lsp_inflight_key.empty()) {
    if (now_ms - completion->lsp_last_request_ms < kLiveCompletionInflightMaxMs) {
      return true;
    }
  }
  return true;
}

bool live_completion_awaiting_lsp(const CompletionState* completion,
                                  MainLayoutState* layout_state,
                                  const std::shared_ptr<ISymbolProvider>& symbols,
                                  const std::string& buffer_path,
                                  const std::string& buffer_text) {
  if (completion == nullptr || !completion->open || !completion->live_mode) {
    return false;
  }
  return completion_awaiting_async_lsp(completion, layout_state, symbols, buffer_path,
                                     buffer_text);
}

void maybe_close_completion_if_empty(CompletionState* completion, MainLayoutState* layout_state,
                                     const std::shared_ptr<ISymbolProvider>& symbols,
                                     const std::string& buffer_path,
                                     const std::string& buffer_text) {
  if (completion == nullptr || !completion->open || !completion->matches.empty()) {
    return;
  }
  if (completion_awaiting_async_lsp(completion, layout_state, symbols, buffer_path, buffer_text)) {
    return;
  }
  completion->close(layout_state);
}

bool completion_ui_show_loading(const CompletionState* completion,
                                MainLayoutState* layout_state,
                                const std::shared_ptr<ISymbolProvider>& symbols,
                                const std::string& buffer_path,
                                const std::string& buffer_text) {
  if (completion == nullptr || !completion->open || !completion->matches.empty()) {
    return false;
  }
  if (completion->has_fallback_items()) {
    return false;
  }
  if (!completion_uses_async_lsp(layout_state, symbols, buffer_path)) {
    return false;
  }
  if (completion->lsp_pending_key.empty() ||
      completion->lsp_pending_key == completion->lsp_resolved_key) {
    return false;
  }
  const int64_t now_ms = steady_now_ms();
  const int64_t wait_ms =
      live_completion_lsp_wait_timeout_ms(buffer_text, completion->lsp_fetch_line);
  if (completion->lsp_request_due_ms <= 0 ||
      now_ms <= completion->lsp_request_due_ms + wait_ms) {
    return true;
  }
  return false;
}

void schedule_completion_lsp_fetch(CompletionState* completion, WorkspaceModel* workspace,
                                 MainLayoutState* layout_state, bool live_only,
                                 EditorPanelState* panel = nullptr) {
  if (completion == nullptr || workspace == nullptr || layout_state == nullptr ||
      layout_state->app_settings == nullptr || !layout_state->app_settings->lsp_enabled) {
    return;
  }
  if (!layout_state->activity_gate.allows_lsp_ui()) {
    return;
  }
  if (live_only && !layout_state->app_settings->live_lsp_completion_enabled) {
    return;
  }
  if (!completion->open) {
    return;
  }
  if (live_only && !completion->live_mode) {
    return;
  }
  workspace->ensure_buffer();
  const EditorBuffer& buffer = workspace->buffer;
  if (live_only && completion->prefix.empty() &&
      !is_member_access_at_cursor(buffer)) {
    return;
  }
  if (buffer.path.empty() || !is_indexed_source_path(buffer.path)) {
    return;
  }
  completion->sync_scope_cache(buffer, panel);
  const std::string anchor_key =
      completion_anchor_key(buffer.path, completion->replace_line, completion->replace_start);
  if (anchor_key.empty()) {
    return;
  }
  if (anchor_key != completion->lsp_fetch_key) {
    completion->lsp_fetch_key = anchor_key;
    completion->lsp_items.clear();
    completion->lsp_resolved_query.clear();
    completion->lsp_resolved_key.clear();
    completion->lsp_inflight_key.clear();
    completion->lsp_fetch_line = buffer.primary_line();
    completion->lsp_fetch_col = buffer.primary_col();
    completion->invalidate_item_cache();
  }
  completion->lsp_pending_key = anchor_key;
  if (completion->lsp_resolved_key != anchor_key) {
    const int64_t debounce =
        live_only ? ((completion->prefix.size() > 1 || is_member_access_at_cursor(buffer))
                         ? kLiveCompletionDebounceMs
                         : 0)
                  : 0;
    completion->lsp_request_due_ms = steady_now_ms() + debounce;
  }
}

void schedule_live_lsp_fetch(CompletionState* completion, WorkspaceModel* workspace,
                             MainLayoutState* layout_state, EditorPanelState* panel = nullptr) {
  schedule_completion_lsp_fetch(completion, workspace, layout_state, true, panel);
}

bool try_poll_lsp_completion(CompletionState* completion, WorkspaceModel* workspace,
                             const std::shared_ptr<ISymbolProvider>& symbols,
                             EditorPanelState* panel, const std::string& key) {
  if (completion == nullptr || workspace == nullptr || symbols == nullptr || key.empty()) {
    return false;
  }
  if (const auto polled = symbols->poll_completion(key)) {
    workspace->ensure_buffer();
    return completion->apply_polled_lsp_response(key, std::move(*polled), workspace->buffer,
                                                 panel);
  }
  return false;
}

void completion_lsp_tick(CompletionState* completion, WorkspaceModel* workspace,
                         const std::shared_ptr<ISymbolProvider>& symbols,
                         MainLayoutState* layout_state, EditorPanelState* panel) {
  if (completion == nullptr || workspace == nullptr || layout_state == nullptr ||
      !completion->open) {
    return;
  }
  const bool live = completion->live_mode;
  if (layout_state->app_settings == nullptr || !layout_state->app_settings->lsp_enabled) {
    return;
  }
  if (live && !layout_state->app_settings->live_lsp_completion_enabled) {
    return;
  }
  if (symbols == nullptr || !symbols->supports_semantic_completion() ||
      !symbols->completion_uses_async_fetch()) {
    return;
  }
  // Live completion: debounce in schedule_completion_lsp_fetch is enough; do not
  // also wait for editor_content_settled (that doubled the 120 ms latency).
  if (!live && panel != nullptr && !editor_content_settled(*panel)) {
    return;
  }

  if (!completion->lsp_inflight_key.empty() &&
      completion->lsp_inflight_key != completion->lsp_pending_key) {
    completion->lsp_inflight_key.clear();
  }

  const int64_t now_ms = steady_now_ms();
  workspace->ensure_buffer();
  const EditorBuffer& buffer = workspace->buffer;
  const std::string buffer_path = buffer.path;
  const std::string buffer_text_snapshot = buffer_text(buffer);
  const int fetch_line = completion->lsp_fetch_line;
  const int64_t lsp_wait_ms =
      live_completion_lsp_wait_timeout_ms(buffer_text_snapshot, fetch_line);
  if (completion_uses_async_lsp(layout_state, symbols, buffer_path) &&
      !completion->lsp_pending_key.empty() &&
      completion->lsp_pending_key != completion->lsp_resolved_key &&
      completion->lsp_request_due_ms > 0 &&
      now_ms > completion->lsp_request_due_ms + lsp_wait_ms) {
    if (!try_poll_lsp_completion(completion, workspace, symbols, panel,
                                 completion->lsp_pending_key)) {
      completion->lsp_resolved_key = completion->lsp_pending_key;
      completion->lsp_resolved_query = completion->query;
      completion->lsp_inflight_key.clear();
      completion->refresh_matches();
      maybe_close_completion_if_empty(completion, layout_state, symbols, buffer_path,
                                      buffer_text_snapshot);
    } else {
      completion->refresh_matches();
      if (completion->matches.empty()) {
        maybe_close_completion_if_empty(completion, layout_state, symbols, buffer_path,
                                        buffer_text_snapshot);
      }
    }
    layout_state->request_ui_tick = true;
    return;
  }

  const auto poll_completion_key = [&](const std::string& key) -> bool {
    if (!try_poll_lsp_completion(completion, workspace, symbols, panel, key)) {
      return false;
    }
    completion->refresh_matches();
    if (completion->matches.empty()) {
      maybe_close_completion_if_empty(completion, layout_state, symbols, buffer_path,
                                      buffer_text_snapshot);
    } else {
      layout_state->request_ui_tick = true;
    }
    return true;
  };

  if (!completion->lsp_inflight_key.empty()) {
    if (poll_completion_key(completion->lsp_inflight_key)) {
      return;
    }
  } else if (!completion->lsp_pending_key.empty() &&
             completion->lsp_pending_key != completion->lsp_resolved_key) {
    if (poll_completion_key(completion->lsp_pending_key)) {
      return;
    }
  }

  if (completion->lsp_inflight_key.empty()) {
    if (!completion->lsp_pending_key.empty() &&
        completion->lsp_pending_key == completion->lsp_resolved_key &&
        !completion->lsp_items.empty()) {
      return;
    }
    if (!completion->lsp_pending_key.empty() && now_ms < completion->lsp_request_due_ms) {
      return;
    }
  }

  if (completion->lsp_pending_key.empty() || now_ms < completion->lsp_request_due_ms) {
    return;
  }
  if (!completion->lsp_inflight_key.empty()) {
    return;
  }
  if (completion->lsp_pending_key == completion->lsp_resolved_key) {
    return;
  }
  if (now_ms - completion->lsp_last_request_ms < kLiveCompletionMinIntervalMs) {
    return;
  }
  if (buffer.path.empty() || !is_indexed_source_path(buffer.path)) {
    return;
  }

  CompletionParams params;
  params.path = buffer.path;
  params.text = buffer_text_snapshot;
  params.line = completion->lsp_fetch_line;
  params.character = completion->lsp_fetch_col;
  symbols->flush_document_sync(buffer.path);
  symbols->request_completion(params, completion->lsp_pending_key);
  completion->lsp_inflight_key = completion->lsp_pending_key;
  completion->lsp_last_request_ms = now_ms;
  if (try_poll_lsp_completion(completion, workspace, symbols, panel, completion->lsp_pending_key)) {
    completion->refresh_matches();
    if (!completion->matches.empty()) {
      layout_state->request_ui_tick = true;
    }
  }
}

void update_live_completion(CompletionState* completion, WorkspaceModel* workspace,
                            const std::shared_ptr<ISymbolProvider>& symbols,
                            SymbolWorkspaceIndexer* symbol_indexer,
                            MainLayoutState* layout_state, EditorBuffer* buffer,
                            EditorPanelState* panel = nullptr) {
  if (completion == nullptr || workspace == nullptr || symbols == nullptr ||
      buffer->path.empty()) {
    return;
  }
  if (!completion_allowed_at_cursor(*buffer)) {
    completion->close(layout_state);
    return;
  }

  prepare_completion_at_cursor(completion, buffer, panel);
  if (completion->prefix.empty() && !is_member_access_at_cursor(*buffer)) {
    completion->close(layout_state);
    return;
  }

  completion->open = true;
  completion->live_mode = true;
  if (!buffer->path.empty() && is_indexed_source_path(buffer->path)) {
    completion->snippet_items = structure_snippet_completions("");
  } else {
    completion->snippet_items.clear();
  }
  completion->refresh_ts_fallback(*buffer);
  completion->invalidate_item_cache();
  completion->refresh_matches();
  schedule_live_lsp_fetch(completion, workspace, layout_state, panel);
  maybe_close_completion_if_empty(completion, layout_state, symbols, buffer->path,
                                  buffer_text(*buffer));
  if (layout_state != nullptr &&
      (completion->open || completion_awaiting_async_lsp(completion, layout_state, symbols,
                                                       buffer->path, buffer_text(*buffer)))) {
    layout_state->request_ui_tick = true;
  }
}

void maybe_open_live_completion(CompletionState* completion, WorkspaceModel* workspace,
                                const std::shared_ptr<ISymbolProvider>& symbols,
                                SymbolWorkspaceIndexer* symbol_indexer,
                                MainLayoutState* layout_state, EditorBuffer* buffer,
                                EditorPanelState* panel, char typed) {
  const bool cpp_file = !buffer->path.empty() && is_indexed_source_path(buffer->path);
  buffer->ensure_cursors();
  const std::string prefix = word_at_cursor(*buffer, buffer->primary());
  const bool snippet_live = cpp_file && structure_snippet_prefix_active(prefix);

  if (snippet_live) {
    if (!is_ident_char(typed) || !completion_allowed_at_cursor(*buffer)) {
      if (completion != nullptr && completion->open && completion->live_mode) {
        completion->close(layout_state);
      }
      return;
    }
    if (completion != nullptr && completion->open && !completion->live_mode) {
      return;
    }
    update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state, buffer,
                           panel);
    return;
  }

  const bool member_access_trigger =
      (typed == '.' || typed == '>') && is_member_access_at_cursor(*buffer);
  if (member_access_trigger) {
    if (layout_state != nullptr && layout_state->app_settings != nullptr &&
        !layout_state->app_settings->live_lsp_completion_enabled) {
      if (completion != nullptr && completion->open && completion->live_mode) {
        completion->close(layout_state);
      }
      return;
    }
    if (!live_completion_uses_async_lsp(layout_state, symbols, buffer->path)) {
      if (completion != nullptr && completion->open && completion->live_mode) {
        completion->close(layout_state);
      }
      return;
    }
    if (buffer->path.empty() || !is_indexed_source_path(buffer->path) || symbols == nullptr ||
        !completion_allowed_at_cursor(*buffer)) {
      if (completion != nullptr && completion->open && completion->live_mode) {
        completion->close(layout_state);
      }
      return;
    }
    if (completion != nullptr && completion->open && !completion->live_mode) {
      return;
    }
    update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state, buffer,
                           panel);
    return;
  }

  if (layout_state != nullptr && layout_state->app_settings != nullptr &&
      !layout_state->app_settings->live_lsp_completion_enabled) {
    if (completion != nullptr && completion->open && completion->live_mode) {
      completion->close(layout_state);
    }
    return;
  }
  if (!live_completion_uses_async_lsp(layout_state, symbols, buffer->path)) {
    if (completion != nullptr && completion->open && completion->live_mode) {
      completion->close(layout_state);
    }
    return;
  }
  if (buffer->path.empty() || !is_indexed_source_path(buffer->path)) {
    if (completion != nullptr && completion->open && completion->live_mode) {
      completion->close(layout_state);
    }
    return;
  }
  if (!is_ident_char(typed) || symbols == nullptr ||
      !completion_allowed_at_cursor(*buffer)) {
    if (completion != nullptr && completion->open && completion->live_mode) {
      completion->close(layout_state);
    }
    return;
  }
  if (completion != nullptr && completion->open && !completion->live_mode) {
    return;
  }
  update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state, buffer,
                       panel);
}

int line_number_width(int total_lines) {
  const int digits = std::max(1, static_cast<int>(std::to_string(total_lines).size()));
  return digits + 1;
}

int editor_render_visible_lines(const EditorPanelState& panel, MainLayoutState* layout_state) {
  if (!panel.code_box.IsEmpty()) {
    return visible_line_count(panel.code_box);
  }
  if (layout_state != nullptr && layout_state->primary_editor.visible_line_count) {
    return std::max(layout_state->primary_editor.visible_line_count(), 1);
  }
  return 24;
}

std::string format_line_number(int line_no, int width) {
  std::string text = std::to_string(line_no);
  if (static_cast<int>(text.size()) < width) {
    text = std::string(static_cast<std::size_t>(width - text.size()), ' ') + text;
  }
  return text;
}

bool find_input_active(MainLayoutState* layout_state, const EditorFindState& find) {
  return find.open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorFind;
}

bool goto_input_active(MainLayoutState* layout_state, bool goto_open) {
  return goto_open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorGotoLine;
}

bool completion_input_active(MainLayoutState* layout_state, bool completion_open) {
  return completion_open && layout_state != nullptr &&
         layout_state->text_input_focus == TextInputFocus::EditorCompletion;
}

void activate_find(EditorFindState* find, EditorBuffer* buffer, MainLayoutState* layout_state,
                   FocusManagerState* focus, FocusRegion panel_focus) {
  find->query.clear();
  find->cursor_pos = 0;
  find->reset_search_state();
  find->open = true;
  find->request_matches(*buffer);
  if (focus != nullptr) {
    focus->region = panel_focus;
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::EditorFind;
    layout_state->focus_sync_needed = true;
  }
}

namespace {

std::string sanitize_find_paste(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      if (!out.empty() && out.back() != ' ') {
        out.push_back(' ');
      }
      continue;
    }
    if (static_cast<unsigned char>(ch) >= 32 && ch != 127) {
      out.push_back(ch);
    }
  }
  return out;
}

}  // namespace

bool handle_find_keys(EditorFindState* find, MainLayoutState* layout_state, EditorBuffer* buffer,
                      Event event, int visible_lines) {
  if (find == nullptr || !find->open) {
    return false;
  }

  if (event == Event::Escape) {
    close_find_bar(find);
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (event == Event::Return) {
    find->jump_to_next_match(buffer, visible_lines);
    return true;
  }
  if (event == Event::Backspace) {
    if (find->cursor_pos > 0 && find->cursor_pos <= static_cast<int>(find->query.size())) {
      find->query.erase(static_cast<std::size_t>(find->cursor_pos - 1), 1);
      --find->cursor_pos;
      find->request_matches(*buffer);
    }
    cursor_blink::show();
    return true;
  }
  if (event == Event::Delete) {
    if (find->cursor_pos >= 0 && find->cursor_pos < static_cast<int>(find->query.size())) {
      find->query.erase(static_cast<std::size_t>(find->cursor_pos), 1);
      find->request_matches(*buffer);
    }
    cursor_blink::show();
    return true;
  }
  if (event == Event::ArrowLeft) {
    find->cursor_pos = std::max(0, find->cursor_pos - 1);
    cursor_blink::show();
    return true;
  }
  if (event == Event::ArrowRight) {
    find->cursor_pos =
        std::min(static_cast<int>(find->query.size()), find->cursor_pos + 1);
    cursor_blink::show();
    return true;
  }
  if (event_is_ctrl_v(event)) {
    const std::string pasted = sanitize_find_paste(read_clipboard_for_paste());
    if (!pasted.empty()) {
      find->query.insert(static_cast<std::size_t>(find->cursor_pos), pasted);
      find->cursor_pos += static_cast<int>(pasted.size());
      find->request_matches(*buffer);
    }
    cursor_blink::show();
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (!ch.empty() && ch[0] >= 32 && ch[0] != 127) {
      find->query.insert(static_cast<std::size_t>(find->cursor_pos), ch);
      find->cursor_pos += static_cast<int>(ch.size());
      find->request_matches(*buffer);
    }
    cursor_blink::show();
    return true;
  }
  return true;
}

bool handle_editor_escape(EditorBuffer* buffer, EditorFindState* find,
                          MainLayoutState* layout_state, bool* goto_open,
                          CompletionState* completion, EditorPanelState* panel,
                          DiagnosticModalState* diagnostic_modal,
                          GitHistoryModalState* git_modal) {
  if (panel != nullptr) {
    end_mouse_selection(panel);
  }
  if (git_modal != nullptr && git_modal->open) {
    git_modal->open = false;
    return true;
  }
  if (diagnostic_modal != nullptr && diagnostic_modal->open) {
    diagnostic_modal->open = false;
    diagnostic_modal->items.clear();
    return true;
  }
  if (completion != nullptr && completion->open) {
    completion->close(layout_state);
    return true;
  }
  if (find != nullptr && find->open) {
    close_find_bar(find);
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (goto_open != nullptr && *goto_open) {
    *goto_open = false;
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (buffer->multi_cursor_active()) {
    exit_multi_cursor_mode(buffer);
    return true;
  }
  if (buffer->primary().has_selection()) {
    clear_primary_selection(buffer);
    return true;
  }
  return true;
}

CursorPos mouse_to_cursor(const Mouse& m, const EditorPanelState& panel, const EditorBuffer& buffer,
                          int visible_lines) {
  const bool in_code = panel.code_box.Contain(m.x, m.y);
  const bool in_gutter = panel.gutter_box.Contain(m.x, m.y);

  int row = 0;
  if (in_code) {
    row = m.y - panel.code_box.y_min;
  } else if (in_gutter) {
    row = m.y - panel.gutter_box.y_min;
  } else if (m.y < panel.code_box.y_min) {
    row = 0;
  } else if (m.y > panel.code_box.y_max) {
    row = visible_lines - 1;
  } else {
    row = m.y - panel.code_box.y_min;
  }

  const int max_line = std::max(0, static_cast<int>(buffer.lines.size()) - 1);
  int line = 0;
  if (!panel.viewport_line_cache.empty() && row >= 0 &&
      row < static_cast<int>(panel.viewport_line_cache.size())) {
    line = panel.viewport_line_cache[static_cast<std::size_t>(row)];
  } else {
    line = std::max(0, std::min(buffer.scroll + row, max_line));
  }

  int col = 0;
  if (in_code) {
    const int tab_size = std::max(1, editor_indent::tab_display_width());
    const std::string& line_text = buffer.lines[static_cast<std::size_t>(line)];
    const int scroll_visual =
        byte_index_to_visual_column(line_text, buffer.scroll_col, tab_size);
    const int visual_col = std::max(0, m.x - panel.code_box.x_min + scroll_visual);

    col = visual_column_to_byte_index(line_text, visual_col, tab_size);
    col = std::min(col, static_cast<int>(line_text.size()));
  } else if (m.x >= panel.code_box.x_min) {
    col = static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
  }

  return {line, col};
}

void autoscroll_on_drag(EditorBuffer* buffer, const Mouse& m, const EditorPanelState& panel,
                        int visible_lines, int code_width) {
  if (m.y < panel.code_box.y_min && buffer->scroll > 0) {
    scroll_view_by_lines(buffer, -1, visible_lines);
  } else if (m.y > panel.code_box.y_max) {
    scroll_view_by_lines(buffer, 1, visible_lines);
  }
  if (code_width > 0 && m.x < panel.code_box.x_min) {
    scroll_view_by_columns(buffer, -3, code_width);
  } else if (code_width > 0 && m.x > panel.code_box.x_max) {
    scroll_view_by_columns(buffer, 3, code_width);
  }
}

void begin_mouse_selection(EditorPanelState* panel, Event event) {
  (void)event;
  panel->mouse_selecting = true;
  panel->captured_mouse.reset();
}

void ensure_mouse_capture(EditorPanelState* panel, Event event) {
  if (!panel->mouse_selecting || panel->captured_mouse) {
    return;
  }
  if (event.screen_ != nullptr) {
    panel->captured_mouse = event.screen_->CaptureMouse();
  }
}

void end_mouse_selection(EditorPanelState* panel) {
  panel->mouse_selecting = false;
  panel->line_select_drag = false;
  panel->line_select_anchor = -1;
  panel->word_select_drag = false;
  panel->word_select_anchor_line = -1;
  panel->word_select_anchor_col = -1;
  panel->captured_mouse.reset();
}

void clear_line_select_commit(EditorPanelState* panel) {
  if (panel == nullptr) {
    return;
  }
  panel->line_select_commit_line = -1;
}

void apply_mouse_drag_word_select(EditorBuffer* buffer, EditorPanelState* panel,
                                  const CursorPos& pos, int visible_lines, int code_width) {
  if (panel == nullptr || panel->word_select_anchor_line < 0) {
    return;
  }
  select_words_range(buffer, panel->word_select_anchor_line, panel->word_select_anchor_col,
                     pos.line, pos.col);
  clamp_all_cursors(buffer);
  ensure_scroll_visible(buffer, visible_lines, code_width);
}

void apply_mouse_drag_line_select(EditorBuffer* buffer, EditorPanelState* panel,
                                  const CursorPos& pos, int visible_lines, int code_width) {
  if (panel == nullptr || panel->line_select_anchor < 0) {
    return;
  }
  select_lines_range(buffer, panel->line_select_anchor, pos.line);
  clamp_all_cursors(buffer);
  ensure_scroll_visible(buffer, visible_lines, code_width);
}

void apply_mouse_drag_head(EditorBuffer* buffer, const Mouse& m, const EditorPanelState& panel,
                           int visible_lines, int code_width) {
  autoscroll_on_drag(buffer, m, panel, visible_lines, code_width);
  const CursorPos pos =
      mouse_to_cursor(m, panel, *buffer, visible_lines);
  buffer->primary().head = pos;
  clamp_all_cursors(buffer);
  cursor_blink::show();
  ensure_scroll_visible(buffer, visible_lines, code_width);
}

bool handle_editor_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                         EditorFindState* find, CompletionState* completion,
                         MainLayoutState* layout_state,
                         EditorPanelState* panel, DiagnosticModalState* diagnostic_modal,
                         GitHistoryModalState* git_modal, GitService* git,
                         const std::shared_ptr<ISymbolProvider>& symbols, DebugModel* debug_model,
                         CommandCallback on_command, Event event, int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }

  EditorBuffer* buffer = &workspace->buffer;
  buffer->ensure_cursors();

  const auto& m = event.mouse();
  const bool in_code = panel->code_box.Contain(m.x, m.y);
  const bool in_gutter = panel->gutter_box.Contain(m.x, m.y);
  const bool in_editor = in_code || in_gutter;

  if (m.button == Mouse::Left && m.motion == Mouse::Released &&
      panel->line_select_commit_line >= 0) {
    if (panel->line_select_anchor >= 0) {
      const CursorPos pos =
          mouse_to_cursor(m, *panel, *buffer, visible_lines);
      select_lines_range(buffer, panel->line_select_anchor, pos.line);
    } else {
      select_line_at(buffer, panel->line_select_commit_line);
    }
    clear_line_select_commit(panel);
    end_mouse_selection(panel);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Moved && panel->mouse_selecting) {
    ensure_mouse_capture(panel, event);
    if (panel->line_select_drag) {
      autoscroll_on_drag(buffer, m, *panel, visible_lines, panel->code_width_chars);
      const CursorPos pos =
          mouse_to_cursor(m, *panel, *buffer, visible_lines);
      apply_mouse_drag_line_select(buffer, panel, pos, visible_lines, panel->code_width_chars);
    } else if (panel->word_select_drag) {
      autoscroll_on_drag(buffer, m, *panel, visible_lines, panel->code_width_chars);
      const CursorPos pos =
          mouse_to_cursor(m, *panel, *buffer, visible_lines);
      apply_mouse_drag_word_select(buffer, panel, pos, visible_lines, panel->code_width_chars);
    } else {
      apply_mouse_drag_head(buffer, m, *panel, visible_lines, panel->code_width_chars);
    }
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Released && panel->mouse_selecting) {
    if (!panel->line_select_drag && !panel->word_select_drag) {
      apply_mouse_drag_head(buffer, m, *panel, visible_lines, panel->code_width_chars);
    }
    end_mouse_selection(panel);
    return true;
  }

  if (!in_editor) {
    if (m.motion == Mouse::Moved) {
      clear_hover_state(&panel->hover);
    }
    return false;
  }

  if (m.motion == Mouse::Moved && in_code && !panel->mouse_selecting) {
    return false;
  }

  if (m.motion == Mouse::Moved && !in_code) {
    clear_hover_state(&panel->hover);
    return false;
  }

  if (m.button == Mouse::WheelLeft || (m.shift && m.button == Mouse::WheelUp)) {
    if (tabular_view_ready(panel, *buffer)) {
      scroll_tabular_columns(buffer, panel, -3, panel->code_width_chars);
      clear_hover_state(&panel->hover);
      return true;
    }
    scroll_view_by_columns(buffer, -3, panel->code_width_chars);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelRight || (m.shift && m.button == Mouse::WheelDown)) {
    if (tabular_view_ready(panel, *buffer)) {
      scroll_tabular_columns(buffer, panel, 3, panel->code_width_chars);
      clear_hover_state(&panel->hover);
      return true;
    }
    scroll_view_by_columns(buffer, 3, panel->code_width_chars);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button == Mouse::WheelUp && !m.shift) {
    if (scroll_tabular_lines(workspace, panel, -3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    scroll_view_by_lines(buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown && !m.shift) {
    if (scroll_tabular_lines(workspace, panel, 3, visible_lines)) {
      clear_hover_state(&panel->hover);
      return true;
    }
    scroll_view_by_lines(buffer, 3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button == Mouse::Right && m.motion == Mouse::Pressed && in_code) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    const CursorPos pos =
        mouse_to_cursor(m, *panel, *buffer, visible_lines);
    buffer->reset_to_single_cursor(pos.line, pos.col);
    MultiCursor cursor = buffer->primary();
    cursor.head = pos;
    int start_col = 0;
    int end_col = 0;
    ident_range_at_cursor(*buffer, cursor, &start_col, &end_col);
    const std::string symbol = word_at_cursor(*buffer, cursor);
    if (!symbol.empty() && layout_state != nullptr) {
      const bool show_call_hierarchy =
          symbols != nullptr && symbols->supports_call_hierarchy() &&
          is_lsp_trackable_path(buffer->path);
      context_menu_open_editor_symbol(&layout_state->context_menu, m.x, m.y, pos.line, pos.col,
                                      start_col, end_col, symbol, buffer->path,
                                      show_call_hierarchy, debug_model);
      end_mouse_selection(panel);
      return true;
    }
    if (layout_state != nullptr && !buffer->path.empty() &&
        is_lsp_trackable_path(buffer->path)) {
      const bool show_call_hierarchy =
          symbols != nullptr && symbols->supports_call_hierarchy();
      context_menu_open_editor_background(&layout_state->context_menu, m.x, m.y, buffer->path,
                                          pos.line, pos.col, show_call_hierarchy);
      end_mouse_selection(panel);
      return true;
    }
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    if (layout_state != nullptr && find != nullptr && find->open) {
      close_find_bar(find);
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::None;
      }
    }
    if (completion != nullptr && completion->open) {
      completion->close(layout_state);
    }

    if (in_gutter && handle_fold_gutter_click(panel, buffer, m, visible_lines)) {
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      end_mouse_selection(panel);
      return true;
    }

    if (in_gutter && debug_model != nullptr && on_command && !buffer->path.empty()) {
      const int row = m.y - panel->gutter_box.y_min;
      if (row >= 0 && row < panel->gutter_visible_rows) {
        const int line = gutter_buffer_line_at_row(*panel, row) + 1;
        ToggleBreakpointAtFile(debug_model, buffer->path, line, on_command);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
        end_mouse_selection(panel);
        return true;
      }
    }

    if (in_gutter && git_modal != nullptr && git != nullptr && git->is_repo() &&
        !buffer->path.empty()) {
      if (handle_git_gutter_click(panel, git_modal, git, *buffer, m)) {
        end_mouse_selection(panel);
        return true;
      }
    }

    if (in_gutter && diagnostic_modal != nullptr && symbols != nullptr &&
        symbols->supports_diagnostics() && !buffer->path.empty()) {
      const DocumentDiagnostics& file_diag =
          cached_file_diagnostics(panel, symbols.get(), buffer->path,
                                  workspace->last_buffer_edit_ms, layout_state);
      if (handle_gutter_marker_click(panel, diagnostic_modal, file_diag, m)) {
        end_mouse_selection(panel);
        return true;
      }
    }

    const CursorPos pos =
        mouse_to_cursor(m, *panel, *buffer, visible_lines);
    const bool shift_click = mouse_shift_active(m, event);
    const bool alt_click = mouse_alt_active(m, event);

    const int64_t now_ms = steady_now_ms();
    const bool same_spot = is_same_click_spot(*panel, pos.line, pos.col, now_ms);
    const int click_count = same_spot ? panel->last_click_count + 1 : 1;
    const bool triple_click =
        in_code && !shift_click && !alt_click && !m.control &&
        is_triple_click(*panel, pos.line, pos.col, now_ms);
    const bool double_click =
        in_code && !shift_click && !alt_click && !m.control && click_count == 2;
    panel->last_click_line = pos.line;
    panel->last_click_col = pos.col;
    panel->last_click_ms = now_ms;
    panel->last_click_count = click_count;

    if (triple_click) {
      select_line_at(buffer, pos.line);
      panel->line_select_drag = true;
      panel->line_select_anchor = pos.line;
      panel->line_select_commit_line = pos.line;
      begin_mouse_selection(panel, event);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      return true;
    }

    clear_line_select_commit(panel);

    if (double_click) {
      select_word_at(buffer, pos.line, pos.col);
      panel->word_select_drag = true;
      panel->word_select_anchor_line = buffer->primary().anchor.line;
      panel->word_select_anchor_col = buffer->primary().anchor.col;
      begin_mouse_selection(panel, event);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      return true;
    }

    if (m.control && symbols != nullptr && symbols->supports_navigation() &&
        !buffer->path.empty()) {
      if (try_go_to_symbol(workspace, layout_state, panel, symbols, pos.line, pos.col, false,
                         visible_lines)) {
        return true;
      }
    }

    if ((alt_click || shift_click) && m.control && symbols != nullptr &&
        symbols->supports_navigation() && !buffer->path.empty()) {
      if (try_go_to_symbol(workspace, layout_state, panel, symbols, pos.line, pos.col, true,
                         visible_lines)) {
        return true;
      }
    }

    if (shift_click) {
      if (buffer->multi_cursor_active()) {
        exit_multi_cursor_mode(buffer);
      }
      const CursorPos anchor = buffer->primary().has_selection() ? buffer->primary().anchor
                                                                 : buffer->primary().head;
      buffer->reset_to_single_cursor(anchor.line, anchor.col);
      buffer->primary().anchor = anchor;
      buffer->primary().head = pos;
      clamp_all_cursors(buffer);
      begin_mouse_selection(panel, event);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      return true;
    }

    if (alt_click) {
      if (buffer->multi_cursor_active()) {
        exit_multi_cursor_mode(buffer);
      }
      const CursorPos anchor = buffer->primary().head;
      buffer->reset_to_single_cursor(anchor.line, anchor.col);
      buffer->primary().anchor = anchor;
      buffer->primary().head = pos;
      clamp_all_cursors(buffer);
      begin_mouse_selection(panel, event);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      return true;
    }

    if (pos.line != buffer->primary().head.line || pos.col != buffer->primary().head.col) {
      workspace->record_cursor_jump();
    }
    if (in_code && layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->lsp_hover_on_click_only &&
        layout_state->activity_gate.allows_lsp_ui()) {
      trigger_lsp_hover_at(panel, pos, m.x, m.y);
      if (layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
    }
    buffer->reset_to_single_cursor(pos.line, pos.col);
    begin_mouse_selection(panel, event);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }

  return false;
}

bool handle_goto_line_keys(GotoLineState* goto_state, MainLayoutState* layout_state,
                           WorkspaceModel* workspace, EditorBuffer* buffer, Event event,
                           int visible_lines) {
  if (goto_state == nullptr || !goto_state->open) {
    return false;
  }

  if (event == Event::Escape) {
    goto_state->open = false;
    goto_state->query.clear();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (event == Event::Return) {
    if (!goto_state->query.empty()) {
      char* end = nullptr;
      const long parsed = std::strtol(goto_state->query.c_str(), &end, 10);
      if (end != goto_state->query.c_str() && parsed > 0) {
        if (workspace != nullptr) {
          workspace->record_cursor_jump();
        }
        goto_buffer_line(buffer, static_cast<int>(parsed), visible_lines);
      }
    }
    goto_state->open = false;
    goto_state->query.clear();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::None;
    }
    return true;
  }
  if (event == Event::Backspace) {
    if (!goto_state->query.empty()) {
      goto_state->query.pop_back();
    }
    cursor_blink::show();
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && std::isdigit(static_cast<unsigned char>(ch[0]))) {
      goto_state->query += ch;
    }
    cursor_blink::show();
    return true;
  }
  return true;
}

void open_completion(CompletionState* completion, WorkspaceModel* workspace,
                     const std::shared_ptr<ISymbolProvider>& symbols,
                     SymbolWorkspaceIndexer* symbol_indexer, EditorBuffer* buffer,
                     EditorFindState* find, MainLayoutState* layout_state,
                     EditorPanelState* panel = nullptr) {
  TGDB_MON_SCOPE("editor", "open_completion");
  if (completion == nullptr) {
    return;
  }
  if (!completion_allowed_at_cursor(*buffer)) {
    return;
  }
  if (find != nullptr && find->open) {
    close_find_bar(find);
  }
  completion->open = true;
  completion->live_mode = false;
  completion->prefix = completion_prefix_at_cursor(*buffer, buffer->primary());
  completion->query.clear();
  completion->selected = 0;
  completion->replace_line = buffer->primary().head.line;
  completion_replace_range_at_cursor(*buffer, buffer->primary(), &completion->replace_start,
                                     &completion->replace_end);
  completion->lsp_fetch_key.clear();
  completion->lsp_pending_key.clear();
  completion->lsp_inflight_key.clear();
  completion->lsp_resolved_key.clear();
  completion->lsp_request_due_ms = 0;
  completion->lsp_last_request_ms = 0;
  completion->lsp_items.clear();
  completion->lsp_resolved_query.clear();
  completion->all_items.clear();
  completion->snippet_items.clear();
  completion->matches.clear();
  completion->invalidate_item_cache();
  completion->semantic_mode = false;
  completion->sync_scope_cache(*buffer, panel);
  completion->sync_symbols(workspace, symbols, symbol_indexer, false);
  if (completion_uses_async_lsp(layout_state, symbols, buffer->path)) {
    symbols->flush_document_sync(buffer->path);
    schedule_completion_lsp_fetch(completion, workspace, layout_state, false, panel);
  } else {
    completion->sync_symbols(workspace, symbols, symbol_indexer, true);
  }
  if (!completion->all_items.empty()) {
    completion->lsp_items = std::move(completion->all_items);
    completion->all_items.clear();
    completion->semantic_mode = !completion->lsp_items.empty();
    completion->commit_scope_cache(*buffer, panel, completion->lsp_items);
    completion->invalidate_item_cache();
  }
  completion->refresh_ts_fallback(*buffer);
  completion->refresh_matches();
  if (completion_uses_async_lsp(layout_state, symbols, buffer->path)) {
    layout_state->request_ui_tick = true;
  } else {
    schedule_live_lsp_fetch(completion, workspace, layout_state, panel);
  }
  if (!completion->lsp_items.empty()) {
    completion->lsp_resolved_key = completion->lsp_pending_key;
  }
  if (completion->matches.empty() &&
      !completion_awaiting_async_lsp(completion, layout_state, symbols, buffer->path,
                                     buffer_text(*buffer))) {
    completion->close(layout_state);
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::None;
  }
}

bool accept_completion(CompletionState* completion, EditorBuffer* buffer,
                       MainLayoutState* layout_state, int visible_lines,
                       WorkspaceModel* workspace, EditorPanelState* panel,
                       const std::shared_ptr<ISymbolProvider>& symbols) {
  if (completion == nullptr || completion->matches.empty()) {
    return false;
  }
  completion->selected =
      std::max(0, std::min(completion->selected, static_cast<int>(completion->matches.size()) - 1));
  const auto& item = completion->matches[static_cast<std::size_t>(completion->selected)].item;
  const std::string raw_insert =
      item.insert_text.empty() ? symbol_insert_name(item.label) : item.insert_text;

  const int repl_line =
      item.has_replace_range ? item.replace_line : completion->replace_line;
  const int repl_start =
      item.has_replace_range ? item.replace_start : completion->replace_start;
  const int repl_end = item.has_replace_range ? item.replace_end : completion->replace_end;

  bool paren_already_there = false;
  if (repl_line >= 0 && repl_line < static_cast<int>(buffer->lines.size())) {
    paren_already_there = has_char_at(buffer->lines[static_cast<std::size_t>(repl_line)], repl_end,
                                      '(');
  }

  const bool treat_as_snippet =
      item.insert_format == InsertTextFormat::kSnippet ||
      raw_insert.find('$') != std::string::npos;

  SnippetResult snippet;
  if (treat_as_snippet) {
    snippet = expand_snippet(raw_insert);
    if (paren_already_there) {
      snippet = adjust_snippet_for_existing_open_paren(raw_insert);
    }
  } else {
    snippet.text = raw_insert;
    snippet.caret_col = static_cast<int>(raw_insert.size());
  }

  if (buffer->multi_cursor_active()) {
    apply_completion_at_all_cursors(buffer, snippet);
  } else {
    replace_text_range_with_caret(buffer, repl_line, repl_start, repl_end, snippet.text,
                                  snippet.caret_line_offset, snippet.caret_col,
                                  snippet.sel_start_col, snippet.sel_end_col);
  }
  ensure_scroll_visible(buffer, visible_lines, -1);
  if (workspace != nullptr && symbols != nullptr) {
    notify_editor_buffer_changed(workspace, panel, symbols);
  }
  completion->close(layout_state);
  return true;
}

bool handle_completion_keys(CompletionState* completion, WorkspaceModel* workspace,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              SymbolWorkspaceIndexer* symbol_indexer,
                              MainLayoutState* layout_state, EditorPanelState* panel,
                              EditorBuffer* buffer, Event event, int visible_lines) {
  if (completion == nullptr || !completion->open) {
    return false;
  }

  if (!completion->live_mode) {
    completion->sync_symbols(workspace, symbols, symbol_indexer, false);
  }

  if (event == Event::Escape) {
    completion->close(layout_state);
    return true;
  }

  if (completion->matches.empty()) {
    return false;
  }

  if (event == Event::Return) {
    return accept_completion(completion, buffer, layout_state, visible_lines, workspace, panel,
                           symbols);
  }
  if (event == Event::Tab) {
    completion->selected = std::min(completion->selected + 1,
                                    static_cast<int>(completion->matches.size()) - 1);
    return true;
  }
  if (event == Event::TabReverse) {
    completion->selected = std::max(0, completion->selected - 1);
    return true;
  }
  if (event == Event::ArrowDown || (!completion->live_mode && event == Event::Character('j'))) {
    completion->selected = std::min(completion->selected + 1,
                                    static_cast<int>(completion->matches.size()) - 1);
    return true;
  }
  if (event == Event::ArrowUp || (!completion->live_mode && event == Event::Character('k'))) {
    completion->selected = std::max(0, completion->selected - 1);
    return true;
  }
  if (event_is_completion(event)) {
    completion->selected =
        (completion->selected + 1) % static_cast<int>(completion->matches.size());
    return true;
  }
  if (completion->live_mode) {
    return false;
  }
  if (event == Event::Backspace) {
    if (!completion->query.empty()) {
      completion->query.pop_back();
      completion->selected = 0;
      completion->refresh_matches();
    }
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      const char typed = ch[0];
      if (is_ident_char(typed)) {
        insert_char(buffer, typed);
        ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
        notify_editor_buffer_changed(workspace, panel, symbols);
        completion->live_mode = true;
        if (!completion->all_items.empty()) {
          completion->lsp_items = std::move(completion->all_items);
          completion->all_items.clear();
          completion->semantic_mode = !completion->lsp_items.empty();
          completion->invalidate_item_cache();
        }
        if (layout_state != nullptr) {
          layout_state->text_input_focus = TextInputFocus::None;
        }
        update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                               buffer, panel);
        return true;
      }
      completion->query += ch;
      completion->selected = 0;
      completion->refresh_matches();
    }
    return true;
  }
  return true;
}

void goto_helix_diagnostic(EditorPanelState* panel, EditorBuffer* buffer, int visible_lines,
                           bool forward) {
  if (panel == nullptr || buffer == nullptr) {
    return;
  }
  std::vector<int> lines;
  lines.reserve(panel->diagnostics_by_line.size());
  for (const auto& entry : panel->diagnostics_by_line) {
    if (!entry.second.empty()) {
      lines.push_back(entry.first);
    }
  }
  if (lines.empty()) {
    return;
  }
  std::sort(lines.begin(), lines.end());
  const int current = buffer->primary_line();
  int target = lines.front();
  if (forward) {
    for (int line : lines) {
      if (line > current) {
        target = line;
        break;
      }
    }
    if (target <= current) {
      target = lines.front();
    }
  } else {
    target = lines.back();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
      if (*it < current) {
        target = *it;
        break;
      }
    }
  }
  buffer->reset_to_single_cursor(target, 0);
  ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
  buffer->view_token++;
}

HelixDispatchContext build_helix_dispatch_context(WorkspaceModel* workspace, EditorPanelState* panel,
                                                  EditorFindState* find, GotoLineState* goto_state,
                                                  FocusManagerState* focus,
                                                  MainLayoutState* layout_state,
                                                  const std::shared_ptr<ISymbolProvider>& symbols,
                                                  int visible_lines) {
  HelixDispatchContext ctx;
  ctx.workspace = workspace;
  ctx.buffer = workspace != nullptr ? &workspace->buffer : nullptr;
  ctx.helix = panel != nullptr ? &panel->helix : nullptr;
  ctx.find = find;
  ctx.layout_state = layout_state;
  ctx.focus = focus;
  ctx.panel_focus = panel != nullptr ? panel->panel_focus : FocusRegion::Editor;
  ctx.visible_lines = visible_lines;
  ctx.code_width = panel != nullptr ? panel->code_width_chars : 80;
  ctx.on_buffer_changed = [workspace, panel, symbols]() {
    notify_editor_buffer_changed(workspace, panel, symbols);
  };
  ctx.open_find_bar = [=]() {
    if (workspace == nullptr || find == nullptr) {
      return;
    }
    workspace->ensure_buffer();
    activate_find(find, &workspace->buffer, layout_state, focus, ctx.panel_focus);
  };
  ctx.open_goto_line = [=]() {
    if (goto_state == nullptr) {
      return;
    }
    goto_state->open = true;
    goto_state->query.clear();
    if (layout_state != nullptr) {
      layout_state->text_input_focus = TextInputFocus::EditorGotoLine;
    }
  };
  ctx.go_to_symbol = [=](int line, int col, bool declaration) {
    return try_go_to_symbol(workspace, layout_state, panel, symbols, line, col, declaration,
                            visible_lines);
  };
  if (layout_state != nullptr) {
    ctx.open_quick_file = layout_state->helix_ide.open_quick_file;
    ctx.open_symbol_picker = layout_state->helix_ide.open_symbol_picker;
    ctx.save_file = layout_state->helix_ide.save_file;
    ctx.request_quit = layout_state->helix_ide.request_quit;
  }
  ctx.goto_next_diagnostic = [=]() {
    goto_helix_diagnostic(panel, workspace != nullptr ? &workspace->buffer : nullptr,
                          visible_lines, true);
  };
  ctx.goto_prev_diagnostic = [=]() {
    goto_helix_diagnostic(panel, workspace != nullptr ? &workspace->buffer : nullptr,
                          visible_lines, false);
  };
  ctx.symbols = symbols.get();
  return ctx;
}

bool helix_editor_active(MainLayoutState* layout_state, bool tabular_view) {
  return layout_state != nullptr && layout_state->app_settings != nullptr &&
         layout_state->app_settings->helix_mode_enabled && !tabular_view;
}

bool handle_editor_keys(WorkspaceModel* workspace, FocusManagerState* focus,
                        EditorFindState* find, GotoLineState* goto_state,
                        CompletionState* completion, EditorPanelState* panel,
                        DiagnosticModalState* diagnostic_modal,
                        GitHistoryModalState* git_modal,
                        const std::shared_ptr<ISymbolProvider>& symbols,
                        WorkspaceIndexer* file_indexer,
                        SymbolWorkspaceIndexer* symbol_indexer,
                        MainLayoutState* layout_state, DebugModel* debug_model,
                        CommandCallback on_command, Event event, int visible_lines) {
  if (focus->region != panel->panel_focus) {
    return false;
  }
  if (find != nullptr && find_input_active(layout_state, *find)) {
    return false;
  }

  workspace->ensure_buffer();
  EditorBuffer* buffer = &workspace->buffer;
  buffer->ensure_cursors();
  const bool tabular_view = is_tabular_path(buffer->path);

  if (event_is_alt_left(event)) {
    if (workspace->navigate_cursor_back(visible_lines)) {
      return true;
    }
    return false;
  }
  if (event_is_alt_right(event)) {
    if (workspace->navigate_cursor_forward(visible_lines)) {
      return true;
    }
    return false;
  }

  if (completion != nullptr && completion->open &&
      event_is_completion_trigger(event, layout_state != nullptr &&
                                            layout_state->editor_ctrl_modifier_held)) {
    open_completion(completion, workspace, symbols, symbol_indexer, buffer, find, layout_state,
                    panel);
    return true;
  }

  if (completion != nullptr && completion->open) {
    if (handle_completion_keys(completion, workspace, symbols, symbol_indexer, layout_state, panel,
                               buffer, event, visible_lines)) {
      return true;
    }
    if (completion->live_mode) {
      // Live completion: typing continues in the editor.
    } else if (!completion->matches.empty() && event != Event::Tab && event != Event::Return) {
      return true;
    }
  }

  if (goto_state != nullptr && goto_state->open) {
    return handle_goto_line_keys(goto_state, layout_state, workspace, buffer, event,
                                 visible_lines);
  }

  const bool helix_on = helix_editor_active(layout_state, tabular_view);

  if (helix_on && event == Event::Escape) {
    HelixDispatchContext hctx =
        build_helix_dispatch_context(workspace, panel, find, goto_state, focus, layout_state,
                                     symbols, visible_lines);
    if (dispatch_helix_keys(hctx, event)) {
      sync_helix_layout_status(layout_state, &panel->helix, true);
      return true;
    }
  }

  if (event == Event::Escape) {
    panel->chord_k_pending = false;
    return handle_editor_escape(buffer, find, layout_state,
                                goto_state != nullptr ? &goto_state->open : nullptr, completion,
                                panel, diagnostic_modal, git_modal);
  }
  if (event == Event::CtrlB && debug_model != nullptr && on_command && !buffer->path.empty()) {
    ToggleBreakpointAtFile(debug_model, buffer->path, buffer->primary_line() + 1, on_command);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  if (event_is_completion_trigger(event, layout_state != nullptr &&
                                              layout_state->editor_ctrl_modifier_held)) {
    open_completion(completion, workspace, symbols, symbol_indexer, buffer, find, layout_state,
                    panel);
    return true;
  }
  if (event_is_go_to_definition(event)) {
    try_go_to_symbol(workspace, layout_state, panel, symbols, buffer->primary_line(), buffer->primary_col(),
                     false, visible_lines);
    return true;
  }
  if (event_is_go_to_declaration(event)) {
    try_go_to_symbol(workspace, layout_state, panel, symbols, buffer->primary_line(), buffer->primary_col(),
                     true, visible_lines);
    return true;
  }
  if (event_is_ctrl_g(event)) {
    if (goto_state != nullptr) {
      goto_state->open = true;
      goto_state->query.clear();
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::EditorGotoLine;
      }
    }
    return true;
  }
  if (event_is_ctrl_alt_z(event) || event_is_ctrl_shift_z(event) || event_is_ctrl_y(event)) {
    if (redo_edit(buffer)) {
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      notify_editor_buffer_changed(workspace, panel, symbols);
    }
    return true;
  }
  if (event_is_ctrl_z(event) && !event_input_has_shift_modifier(event)) {
    undo_edit(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    return true;
  }
  if (panel->chord_k_pending) {
    if (event_is_ctrl_c(event)) {
      panel->chord_k_pending = false;
      const LineCommentStyle style = line_comment_style_for_path(buffer->path);
      comment_lines(buffer, style);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      notify_editor_buffer_changed(workspace, panel, symbols);
      return true;
    }
    if (event_is_ctrl_u(event)) {
      panel->chord_k_pending = false;
      const LineCommentStyle style = line_comment_style_for_path(buffer->path);
      uncomment_lines(buffer, style);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      notify_editor_buffer_changed(workspace, panel, symbols);
      return true;
    }
    panel->chord_k_pending = false;
  }
  if (event_is_ctrl_k(event)) {
    panel->chord_k_pending = true;
    return true;
  }
  if (event_is_ctrl_c(event)) {
    copy_selection(buffer);
    return true;
  }
  if (event_is_ctrl_x(event)) {
    cut_selection(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    return true;
  }
  if (event_is_ctrl_v(event)) {
    paste_text(buffer, read_clipboard_for_paste());
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    return true;
  }
  if (event_is_ctrl_u(event)) {
    move_primary_half_page_up(buffer, visible_lines);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_plain_tab(event)) {
    insert_tab_stop(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    return true;
  }
  if (event_is_ctrl_i(event)) {
    move_primary_half_page_down(buffer, visible_lines);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_ctrl_d(event) || event_is_ctrl_alt_d(event) || event_is_ctrl_shift_d(event)) {
    add_next_selection_match(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_alt_l(event) || event_is_ctrl_shift_l(event)) {
    select_all_matches(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::CtrlS) {
    workspace->save_buffer();
    if (!workspace->root.empty() && !workspace->buffer.path.empty()) {
      if (layout_state != nullptr && layout_state->on_file_saved) {
        layout_state->on_file_saved(workspace->buffer.path);
      }
      std::error_code ec;
      const auto rel = std::filesystem::relative(
          std::filesystem::path(workspace->buffer.path),
          std::filesystem::path(workspace->root), ec);
      if (!ec) {
        const std::string rel_str = rel.generic_string();
        if (file_indexer != nullptr) {
          file_indexer->upsert_file(workspace->root, rel_str, workspace->buffer.path);
        }
        if (symbol_indexer != nullptr) {
          symbol_indexer->reindex_file(workspace->root, rel_str, workspace->buffer.path);
        }
      }
    }
    return true;
  }

  if (helix_on) {
    if (event_is_tide_app_shortcut(event) || event_is_ctrl_key_release(event)) {
      return false;
    }
    HelixDispatchContext hctx =
        build_helix_dispatch_context(workspace, panel, find, goto_state, focus, layout_state,
                                     symbols, visible_lines);
    if (dispatch_helix_keys(hctx, event)) {
      sync_helix_layout_status(layout_state, &panel->helix, true);
      if (panel->helix.mode == HelixMode::kInsert && completion != nullptr &&
          completion->open && completion->live_mode &&
          (event == Event::Backspace || event == Event::Delete)) {
        update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                               buffer, panel);
      }
      return true;
    }
    if (panel->helix.mode != HelixMode::kInsert) {
      sync_helix_layout_status(layout_state, &panel->helix, true);
      if (event_has_ctrl_modifier(event) || event_is_tide_app_shortcut(event) ||
          event_is_ctrl_key_release(event)) {
        return false;
      }
      return true;
    }
  } else {
    sync_helix_layout_status(layout_state, &panel->helix, false);
  }

  const bool extend = event_is_shift_left(event) || event_is_shift_right(event) ||
                      event_is_shift_up(event) || event_is_shift_down(event) ||
                      event_is_ctrl_alt_left(event) || event_is_ctrl_alt_right(event) ||
                      event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
                      event_is_shift_home(event) || event_is_shift_end(event);

  if (tabular_view && panel != nullptr && panel->tabular_store != nullptr &&
      panel->tabular_store->ready()) {
    const int data_total = panel->tabular_store->max_data_total();
    const int data_visible = tabular_data_visible_lines(visible_lines);
    const auto scroll_data = [&](int delta) {
      buffer->scroll = std::max(
          0, std::min(buffer->scroll + delta, max_scroll(data_total, data_visible)));
      clamp_tabular_scroll(panel, buffer, visible_lines);
      maybe_request_tabular_chunk(panel, buffer, visible_lines);
      buffer->view_token++;
      return true;
    };
    const auto scroll_cols = [&](int delta) {
      scroll_tabular_columns(buffer, panel, delta, panel->code_width_chars);
      return true;
    };
    if (event == Event::ArrowLeft || event_is_shift_left(event) || event_is_ctrl_left(event) ||
        event_is_ctrl_alt_left(event) || event_is_ctrl_shift_left(event)) {
      return scroll_cols(-10);
    }
    if (event == Event::ArrowRight || event_is_shift_right(event) || event_is_ctrl_right(event) ||
        event_is_ctrl_alt_right(event) || event_is_ctrl_shift_right(event)) {
      return scroll_cols(10);
    }
    if (event == Event::ArrowDown || event_is_shift_down(event)) {
      return scroll_data(1);
    }
    if (event == Event::ArrowUp || event_is_shift_up(event)) {
      return scroll_data(-1);
    }
    if (event == Event::PageDown) {
      return scroll_data(data_visible);
    }
    if (event == Event::PageUp) {
      return scroll_data(-data_visible);
    }
    if (event == Event::Home || event_is_shift_home(event)) {
      buffer->scroll = 0;
      buffer->view_token++;
      return true;
    }
    if (event == Event::End || event_is_shift_end(event)) {
      buffer->scroll = tabular_max_allowed_scroll(panel, visible_lines);
      maybe_request_tabular_chunk(panel, buffer, visible_lines);
      buffer->view_token++;
      return true;
    }
  }

  if (event_is_ctrl_left(event) || event_is_ctrl_alt_left(event) ||
      event_is_ctrl_shift_left(event)) {
    move_primary_word_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_ctrl_right(event) || event_is_ctrl_alt_right(event) ||
      event_is_ctrl_shift_right(event)) {
    move_primary_word_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::ArrowLeft || event_is_shift_left(event)) {
    move_primary_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::ArrowRight || event_is_shift_right(event)) {
    move_primary_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_ctrl_alt_up(event) || event_is_ctrl_shift_up(event)) {
    if (tabular_view) {
      return true;
    }
    extend_block_selection_vertical(buffer, -1);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_ctrl_alt_down(event) || event_is_ctrl_shift_down(event)) {
    if (tabular_view) {
      return true;
    }
    extend_block_selection_vertical(buffer, 1);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::ArrowUp || event_is_shift_up(event)) {
    move_primary_up(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::ArrowDown || event_is_shift_down(event)) {
    move_primary_down(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::Home || event_is_shift_home(event)) {
    move_primary_home(buffer, event_is_shift_home(event));
    return true;
  }
  if (event == Event::End || event_is_shift_end(event)) {
    move_primary_end(buffer, event_is_shift_end(event));
    return true;
  }
  if (tabular_view) {
    if (event == Event::Backspace || event == Event::Delete || event == Event::Return ||
        event_is_ctrl_backspace(event) || event_is_ctrl_delete(event) || event.is_character()) {
      workspace->status_message = i18n::tr("editor.tabular.readonly_status");
      return true;
    }
  }
  if (event_is_ctrl_backspace(event)) {
    delete_word_backward(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer, panel);
    }
    return true;
  }
  if (event_is_ctrl_delete(event)) {
    delete_word_forward(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer, panel);
    }
    return true;
  }
  if (event == Event::Backspace) {
    backspace(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer, panel);
    }
    return true;
  }
  if (event == Event::Delete) {
    delete_char(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer, panel);
    }
    return true;
  }
  if (event == Event::Return) {
    newline(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    return true;
  }
  if (event == Event::PageDown) {
    move_primary_page_down(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event == Event::PageUp) {
    move_primary_page_up(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      const char typed = ch[0];
      insert_char(buffer, typed);
      ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
      notify_editor_buffer_changed(workspace, panel, symbols);
      maybe_open_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                                 buffer, panel, typed);
      return true;
    }
  }
  return false;
}

Element render_completion_fuzzy_chars(const std::string& segment,
                                      const std::unordered_set<std::size_t>& hits,
                                      Color base_color) {
  Elements parts;
  std::string run;
  Color run_color = base_color;
  auto flush = [&]() {
    if (!run.empty()) {
      parts.push_back(text(run) | color(run_color));
      run.clear();
    }
  };
  for (std::size_t i = 0; i < segment.size(); ++i) {
    const Color want = hits.count(i) != 0 ? theme::Error() : base_color;
    if (!run.empty() && want != run_color) {
      flush();
    }
    run_color = want;
    run.push_back(segment[i]);
  }
  flush();
  return parts.size() == 1 ? parts[0] : hbox(std::move(parts));
}

Element make_completion_loading_overlay(const EditorBuffer& buffer, int gutter_width,
                                        int visible_lines) {
  Element popup = text(i18n::tr("panel.outline.loading")) | color(theme::Muted()) |
                  bgcolor(theme::CodeBg());
  const int caret_row = std::max(0, buffer.primary_line() - buffer.scroll);
  const int caret_col = std::max(0, buffer.primary_col());
  const int x_pad = gutter_width + 1;
  const int y_pad = caret_row + 1;
  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, x_pad + caret_col),
                           popup | clear_under,
                           filler()}),
                     filler()}) |
                   flex});
}

Element make_completion_overlay(const CompletionState& completion_state,
                                const EditorBuffer& buffer, int gutter_width,
                                int visible_lines, MainLayoutState* layout_state,
                                const std::shared_ptr<ISymbolProvider>& symbols) {
  if (!completion_state.open) {
    return text("");
  }
  const std::string buffer_text_snapshot = buffer_text(buffer);
  if (completion_state.matches.empty()) {
    if (completion_ui_show_loading(&completion_state, layout_state, symbols, buffer.path,
                                    buffer_text_snapshot)) {
      return make_completion_loading_overlay(buffer, gutter_width, visible_lines);
    }
    return text("");
  }

  const int max_rows = 8;
  const int start = std::max(
      0, std::min(completion_state.selected,
                  std::max(0, static_cast<int>(completion_state.matches.size()) - max_rows)));
  const int end =
      std::min(static_cast<int>(completion_state.matches.size()), start + max_rows);

  Elements rows;
  for (int i = start; i < end; ++i) {
    const CompletionMatch& match = completion_state.matches[static_cast<std::size_t>(i)];
    const CompletionItem& item = match.item;
    const std::string icon = symbol_kind_glyph(item.kind);
    const Color kind_color = theme::ColorForSymbolKind(item.kind);
    const std::unordered_set<std::size_t> hits(match.match_indices.begin(),
                                               match.match_indices.end());
    Element name_el = match.match_indices.empty()
                          ? text(match.match_display) | color(theme::Header())
                          : render_completion_fuzzy_chars(match.match_display, hits, theme::Header());
    Element label_row = hbox({
        text(" " + icon + " ") | color(kind_color),
        name_el,
    });
    if (!item.detail.empty()) {
      label_row = hbox({
          label_row,
          text("  " + item.detail) | color(theme::Muted()),
      });
    }
    Element row = label_row;
    if (i == completion_state.selected) {
      row = row | inverted | bgcolor(theme::EditorLineHi());
    } else {
      row = row | bgcolor(theme::CodeBg());
    }
    rows.push_back(row);
  }

  const int popup_rows = static_cast<int>(rows.size());
  Element popup = vbox(std::move(rows)) | bgcolor(theme::CodeBg());

  const int caret_row = std::max(0, buffer.primary_line() - buffer.scroll);
  const int caret_col = std::max(0, buffer.primary_col());
  const int x_pad = gutter_width + 1;

  const bool place_above = visible_lines > 0 && caret_row + popup_rows + 1 >= visible_lines;
  const int y_pad = place_above ? std::max(0, caret_row - popup_rows) : caret_row + 1;

  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, x_pad + caret_col),
                           popup | clear_under,
                           filler()}),
                     filler()}) |
                   flex});
}

Element make_goto_line_overlay(const GotoLineState& goto_state) {
  if (!goto_state.open) {
    return text("");
  }
  std::string input_line = goto_state.query;
  input_line.push_back('_');
  Element dialog = ModalWindow(
      text(i18n::tr("modal.goto_line.title")) | color(theme::Accent()),
      vbox({ModalInputLine(input_line),
            text(i18n::tr("modal.goto_line.footer")) | color(theme::Muted())}));
  return CenteredModal(std::move(dialog));
}

void flash_symbol_at_buffer_pos_impl(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                     EditorPanelState* panel_state, int line, int col,
                                     int visible_lines) {
  if (workspace == nullptr || layout_state == nullptr) {
    return;
  }
  workspace->ensure_buffer();
  const std::string& path =
      workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
  if (path.empty()) {
    return;
  }
  EditorBuffer& buffer = workspace->buffer;
  MultiCursor cursor;
  cursor.head = {line, col};
  cursor.anchor = {line, col};
  int start_col = 0;
  int end_col = 0;
  if (ident_range_at_cursor(buffer, cursor, &start_col, &end_col)) {
    const int render_row = line - buffer.scroll;
    request_editor_symbol_press(layout_state, normalize_path(path), line, start_col, end_col,
                                render_row);
    buffer.view_token++;
  }
}

}  // namespace

bool editor_deferred_sync_allowed(MainLayoutState* layout_state) {
  if (layout_state == nullptr) {
    return true;
  }
  return layout_state->activity_gate.allows_deferred_editor_sync(steady_now_ms());
}

void flash_symbol_at_buffer_pos(WorkspaceModel* workspace, MainLayoutState* layout_state, int line,
                                int col, int visible_lines) {
  flash_symbol_at_buffer_pos_impl(workspace, layout_state, nullptr, line, col, visible_lines);
}

Component MakeEditorPanel(WorkspaceModel* workspace, FocusManagerState* focus,
                          MainLayoutState* layout_state,
                          std::shared_ptr<ISymbolProvider> symbols,
                          WorkspaceIndexer* file_indexer,
                          SymbolWorkspaceIndexer* symbol_indexer,
                          GitService* git_service, FocusRegion panel_focus,
                          DebugModel* debug_model, CommandCallback on_command,
                          EditorPanelHandlers* handlers) {
  auto panel_state = std::make_shared<EditorPanelState>();
  panel_state->panel_focus = panel_focus;
  auto tab_bar_state = std::make_shared<EditorTabBarState>();
  auto find_state = std::make_shared<EditorFindState>();
  auto goto_state = std::make_shared<GotoLineState>();
  auto completion_state = std::make_shared<CompletionState>();
  auto diagnostic_state = std::make_shared<DiagnosticModalState>();
  auto git_history_state = std::make_shared<GitHistoryModalState>();

  auto modal_overlay = Renderer([find_state, goto_state, completion_state,
                                 diagnostic_state, git_history_state, workspace, symbols,
                                 symbol_indexer, panel_state, tab_bar_state, layout_state,
                                 focus] {
    if (layout_state != nullptr) {
      layout_state->editor_completion_open = completion_state->open;
    }
    if (completion_state->open && focus != nullptr &&
        focus->region != panel_state->panel_focus) {
      completion_state->close(layout_state);
      if (layout_state != nullptr) {
        layout_state->editor_completion_open = false;
      }
      return text("");
    }
    if (tab_bar_state->overflow_open) {
      return make_tabs_overflow_modal(workspace, tab_bar_state.get());
    }
    if (layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->helix_mode_enabled) {
      if (panel_state->helix.help_open) {
        return make_helix_help_overlay(panel_state->helix);
      }
      if (panel_state->helix.command_mode) {
        return make_helix_command_overlay(panel_state->helix);
      }
      if (panel_state->helix.hint_visible && panel_state->helix.prefix_active()) {
        return make_helix_hint_overlay(panel_state->helix);
      }
    }
    if (git_history_state->open) {
      return make_git_history_modal(*git_history_state);
    }
    if (diagnostic_state->open) {
      return make_diagnostic_modal(*diagnostic_state);
    }
    if (completion_state->open) {
      workspace->ensure_buffer();
      const EditorBuffer& buffer = workspace->buffer;
      const int total = static_cast<int>(buffer.lines.size());
      const int gutter_w = line_number_width(total);
      const int visible = visible_line_count(panel_state->code_box);
      return make_completion_overlay(*completion_state, buffer, gutter_w, visible, layout_state,
                                     symbols);
    }
    if (goto_state->open) {
      return make_goto_line_overlay(*goto_state);
    }
    if (!find_state->open && !completion_state->open && !diagnostic_state->open &&
        !git_history_state->open) {
      return make_hover_tooltip(panel_state->hover, panel_state->code_box);
    }
    if (!find_state->open) {
      return text("");
    }
    const bool find_focused = layout_state != nullptr &&
                              layout_state->text_input_focus == TextInputFocus::EditorFind;
    Element find_row =
        hbox({text(i18n::tr("editor.find.label")) | color(theme::Muted()),
              RenderBlinkInputLine(find_state->query, find_state->cursor_pos, find_focused) | flex,
              text(i18n::tr_fmt("editor.find.count",
                                {std::to_string(find_state->matches.size())})) |
                  color(theme::Muted())}) |
        border | bgcolor(theme::PanelBg()) | size(WIDTH, GREATER_THAN, 34) | size(HEIGHT, EQUAL, 3);
    return dbox({text(""),
                 vbox({
                     hbox({filler(), std::move(find_row)}),
                     filler(),
                 }) |
                     flex});
  });

  auto code_view = Renderer([workspace, focus, panel_state, find_state, symbols, layout_state,
                             git_service, debug_model] {
    workspace->ensure_buffer();
    UiPerfMonitor* ui_perf =
        layout_state != nullptr ? &layout_state->ui_perf_monitor : nullptr;
    UiSyncPhaseScope render_scope(ui_perf, "render.editor");
    EditorBuffer& buffer = workspace->buffer;
    buffer.ensure_cursors();

    if (buffer.path.empty()) {
      return vbox({
                 filler(),
                 hbox({filler(), RenderTuideLogo(), filler()}),
                 filler(),
             }) |
             flex | bgcolor(theme::CodeBg()) | reflect(panel_state->code_box);
    }

    const bool path_changed = buffer.path != panel_state->last_path;

    if (path_changed) {
      panel_state->last_path = buffer.path;
      buffer.collapsed_folds.clear();
      buffer.fold_regions.clear();
      panel_state->fold_regions_cache_path.clear();
      panel_state->fold_regions_cache_token = 0;
      panel_state->colored_brace_cache.clear();
      panel_state->colored_brace_cache_path.clear();
      panel_state->colored_brace_cache_token = 0;
      panel_state->viewport_line_render_cache.clear();
      panel_state->line_syntax_span_cache.clear();
      panel_state->line_syntax_span_cache_scroll_col = -1;
      panel_state->scope_completion_cache_key.clear();
      panel_state->scope_completion_cache_items.clear();
      panel_state->cached_symbols_path.clear();
      panel_state->cached_semantic_path.clear();
      panel_state->last_semantic_highlight_revision = 0;
      panel_state->semantic_tokens_layout_stale = false;
      panel_state->semantic_tokens_enqueue_pending =
          !buffer.path.empty() && !is_tabular_path(buffer.path);
      panel_state->last_semantic_highlight_revision_tick = 0;
      buffer.scroll_col = 0;
      panel_state->h_scrollbar_dragging = false;
      panel_state->h_scrollbar_layout = {};
      clear_hover_state(&panel_state->hover);
      panel_state->document_open_pending = !buffer.path.empty();
      panel_state->pending_document_open_path = buffer.path;
      panel_state->tabular_layout_path.clear();
      panel_state->tabular_layout_line_count = 0;
      panel_state->tabular_store.reset();
      panel_state->tabular_scroll_locked = false;
      panel_state->tabular_scroll_limit = 0;
      if (is_tabular_path(buffer.path)) {
        panel_state->tabular_store = std::make_unique<TabularFileStore>();
        panel_state->tabular_store->open_async(buffer.path);
      }
      if (layout_state != nullptr && layout_state->schedule_ui_tick) {
        layout_state->schedule_ui_tick();
      }
      buffer.scroll = std::max(0, buffer.primary_line() - 2);
    }

    const int total = static_cast<int>(buffer.lines.size());
    const bool tabular_view = is_tabular_path(buffer.path);
    TabularFileStore* tabular_store =
        tabular_view ? panel_state->tabular_store.get() : nullptr;
    const bool tabular_ready = tabular_store != nullptr && tabular_store->ready();
    const bool tabular_indexing = tabular_store != nullptr && tabular_store->indexing();

    if (tabular_view && tabular_store != nullptr) {
      if (layout_state != nullptr && layout_state->schedule_ui_tick) {
        const int64_t now = steady_now_ms();
        if (tabular_indexing || tabular_store->loading_more() ||
            (tabular_ready && tabular_store->has_more())) {
          if (now - panel_state->last_tabular_index_tick_ms >= 200) {
            panel_state->last_tabular_index_tick_ms = now;
            layout_state->schedule_ui_tick();
          }
        }
      }
    }

    if (buffer.view_token != panel_state->last_view_token) {
      panel_state->last_view_token = buffer.view_token;
    }

    const uint64_t ts_highlight_revision =
        !buffer.path.empty() ? tree_sitter_service().revision_for(buffer.path) : 0;

    const bool indexed_cpp =
        !buffer.path.empty() && is_indexed_source_path(buffer.path);
    const bool typing_burst = !editor_content_settled(*panel_state);
    const bool typing_edit_mode = typing_burst && indexed_cpp;
    const bool defer_sticky_scroll = typing_edit_mode;

    const SemanticTokenDocument* semantic_tokens = nullptr;
    uint64_t semantic_source_generation = 0;
    if (symbols && symbols->supports_semantic_highlight() && indexed_cpp) {
      const bool tokens_current = symbols->semantic_tokens_current_for_file(buffer.path);
      const bool can_refresh_semantic =
          !panel_state->semantic_tokens_layout_stale || editor_content_settled(*panel_state);
      if (!typing_edit_mode && can_refresh_semantic) {
        const uint64_t semantic_rev = symbols->semantic_highlight_revision();
        if (panel_state->cached_semantic_path != buffer.path ||
            semantic_rev != panel_state->last_semantic_highlight_revision) {
          const SemanticTokenDocument fresh = symbols->semantic_tokens_for_file(buffer.path);
          if (fresh.ready && semantic_tokens_match_document(*symbols, buffer.path, fresh)) {
            panel_state->cached_semantic_path = buffer.path;
            panel_state->last_semantic_highlight_revision = semantic_rev;
            panel_state->cached_semantic_tokens = fresh;
            panel_state->semantic_tokens_layout_stale = false;
            panel_state->viewport_line_render_cache.clear();
            panel_state->line_syntax_span_cache.clear();
          } else if (panel_state->cached_semantic_path != buffer.path) {
            panel_state->cached_semantic_path = buffer.path;
            panel_state->last_semantic_highlight_revision = semantic_rev;
            panel_state->cached_semantic_tokens = fresh;
          }
        }
        if (!tokens_current ||
            !semantic_tokens_match_document(*symbols, buffer.path,
                                            panel_state->cached_semantic_tokens)) {
          panel_state->semantic_tokens_enqueue_pending = true;
        }
      } else if (panel_state->semantic_tokens_layout_stale) {
        panel_state->semantic_tokens_enqueue_pending = true;
      }
      if (!panel_state->semantic_tokens_layout_stale && tokens_current &&
          panel_state->cached_semantic_path == buffer.path &&
          panel_state->cached_semantic_tokens.ready &&
          semantic_tokens_match_document(*symbols, buffer.path,
                                         panel_state->cached_semantic_tokens)) {
        semantic_tokens = &panel_state->cached_semantic_tokens;
        semantic_source_generation = panel_state->cached_semantic_tokens.source_generation;
      }
    }

    const int visible = visible_line_count(panel_state->code_box);

    const bool scope_highlight_enabled =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->scope_highlight_enabled;
    const bool scope_visual_effects =
        editor_scope_effects_allowed(layout_state, scope_highlight_enabled);
    const int scope_highlight_strength =
        layout_state != nullptr && layout_state->app_settings != nullptr
            ? layout_state->app_settings->scope_highlight_strength
            : 58;

    const bool path_indexed_cpp =
        !buffer.path.empty() && is_indexed_source_path(buffer.path);
    if (scope_visual_effects && path_indexed_cpp) {
      const bool path_changed = panel_state->fold_regions_cache_path != buffer.path;
      const bool settled = editor_content_settled(*panel_state);
      const bool token_changed =
          panel_state->fold_regions_cache_token != buffer.view_token;
      if (path_changed || (settled && token_changed)) {
        const std::vector<FoldRegion> fresh =
            tree_sitter_service().fold_regions_at(buffer.path, buffer.lines);
        if (!fresh.empty() || path_changed || buffer.fold_regions.empty()) {
          buffer.fold_regions = fresh;
          std::set<int> valid_open_lines;
          for (const FoldRegion& region : buffer.fold_regions) {
            valid_open_lines.insert(region.open_line);
          }
          for (auto it = buffer.collapsed_folds.begin(); it != buffer.collapsed_folds.end();) {
            if (valid_open_lines.count(*it) == 0) {
              it = buffer.collapsed_folds.erase(it);
            } else {
              ++it;
            }
          }
          panel_state->fold_regions_cache_path = buffer.path;
          panel_state->fold_regions_cache_token = buffer.view_token;
        }
      }
    } else if (!path_indexed_cpp) {
      buffer.fold_regions.clear();
      buffer.collapsed_folds.clear();
    }
    const bool fold_gutter_enabled =
        scope_visual_effects && path_indexed_cpp && !buffer.fold_regions.empty();
    panel_state->gutter_fold_width = fold_gutter_enabled ? 1 : 0;

    const std::vector<int> viewport_lines =
        viewport_buffer_lines(buffer, buffer.fold_regions, visible);
    panel_state->viewport_line_cache = viewport_lines;
    panel_state->gutter_scroll_start =
        viewport_lines.empty() ? buffer.scroll : viewport_lines.front();
    panel_state->gutter_visible_rows = static_cast<int>(viewport_lines.size());

    const int scroll_total =
        buffer.collapsed_folds.empty()
            ? total
            : static_cast<int>(visible_buffer_lines(total, buffer.fold_regions,
                                                    buffer.collapsed_folds)
                                   .size());
    int scroll_visible_index = 0;
    if (!buffer.collapsed_folds.empty()) {
      const std::vector<int> all_visible =
          visible_buffer_lines(total, buffer.fold_regions, buffer.collapsed_folds);
      const int found = visible_line_index(all_visible, buffer.scroll);
      scroll_visible_index = found >= 0 ? found : 0;
    } else {
      scroll_visible_index = buffer.scroll;
    }

    const bool helix_relative =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->helix_mode_enabled;
    const int gutter_w = helix_gutter_width(total, visible, helix_relative) + panel_state->gutter_fold_width;
    const bool editor_focused = focus->region == panel_state->panel_focus;

    if (tabular_view && tabular_store != nullptr) {
      Elements gutter_rows;
      Elements code_rows;
      const Decorator row_bg = bgcolor(theme::CodeBg());
      const Decorator header_bg = bgcolor(theme::TabIdle());

      if (tabular_indexing) {
        gutter_rows.push_back(text(format_line_number(1, 4)) | color(theme::Muted()) | row_bg);
        code_rows.push_back(text(i18n::tr("editor.tabular.indexing")) | color(theme::Muted()) | row_bg);
      } else if (!tabular_ready) {
        gutter_rows.push_back(text(format_line_number(1, 4)) | color(theme::Muted()) | row_bg);
        const std::string message =
            tabular_store->error_message().empty()
                ? i18n::tr("editor.tabular.open_failed")
                : tabular_store->error_message();
        code_rows.push_back(text(message) | color(theme::Error()) | row_bg);
      } else {
        const int file_rows = tabular_store->row_count();
        const int data_total = tabular_store->max_data_total();
        const int data_visible = tabular_data_visible_lines(visible);
        clamp_tabular_scroll(panel_state.get(), &buffer, visible);
        tabular_store->ensure_viewport(buffer.scroll, data_visible);
        maybe_request_tabular_chunk(panel_state.get(), &buffer, visible);

        const int data_scroll = std::max(0, std::min(buffer.scroll, max_scroll(data_total, 1)));
        const TabularTableLayout& layout = tabular_store->layout();
        const TabularDelimiter delimiter = tabular_store->delimiter();
        const int gutter_w = line_number_width(std::max(file_rows, 1));
        const int code_width =
            code_width_from_box(panel_state->code_box, panel_state->code_width_chars);
        panel_state->code_width_chars = code_width;
        clamp_tabular_scroll_col(&buffer, panel_state.get(), code_width);

        const std::string header_line = tabular_store->row_at(0);
        const auto header_cells = parse_tabular_row(header_line, delimiter);
        gutter_rows.push_back(text(format_line_number(1, gutter_w)) | color(theme::Muted()) |
                              header_bg);
        code_rows.push_back(render_tabular_row_viewport(header_cells, layout, true,
                                                        buffer.scroll_col, code_width) |
                            size(WIDTH, EQUAL, code_width) | xflex_shrink | header_bg);

        const int data_end = std::min(data_total, data_scroll + data_visible);
        for (int data_index = data_scroll; data_index < data_end; ++data_index) {
          const int row_index = data_index + 1;
          const std::string line = tabular_store->row_at(row_index);
          const auto cells = parse_tabular_row(line, delimiter);
          gutter_rows.push_back(text(format_line_number(row_index + 1, gutter_w)) |
                              color(theme::Muted()) | row_bg);
          code_rows.push_back(render_tabular_row_viewport(cells, layout, false, buffer.scroll_col,
                                                          code_width) |
                              size(WIDTH, EQUAL, code_width) | xflex_shrink | row_bg);
        }

        if (tabular_store->loading_more()) {
          gutter_rows.push_back(text(format_line_number(data_end + 2, gutter_w)) |
                              color(theme::Muted()) | row_bg);
          code_rows.push_back(text(i18n::tr("editor.tabular.loading_rows")) | color(theme::Muted()) |
                              row_bg);
        } else if (code_rows.size() == 1) {
          gutter_rows.push_back(text(format_line_number(2, gutter_w)) | color(theme::Muted()) |
                                row_bg);
          code_rows.push_back(text(i18n::tr("editor.tabular.no_data_rows")) | color(theme::Muted()) |
                              row_bg);
        }

        const int rendered_lines = static_cast<int>(code_rows.size());
        Element gutter = vbox(std::move(gutter_rows)) | reflect(panel_state->gutter_box) |
                         bgcolor(theme::CodeBg());
        Element code = vbox(std::move(code_rows)) | flex | reflect(panel_state->code_box) |
                       bgcolor(theme::CodeBg());
        const bool scroll_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kEditorScrollbar);
        const bool scroll_active =
            panel_state->scrollbar_dragging ||
            (layout_state != nullptr &&
             layout_state->clickable.is_pressed(press_id::kEditorScrollbar));
        Element scrollbar =
            vertical_scrollbar(data_total, data_scroll, data_visible, rendered_lines,
                               scroll_hovered, scroll_active) |
            reflect(panel_state->scrollbar_box);
        panel_state->scrollbar_layout =
            compute_scrollbar_layout(data_total, data_scroll, data_visible, rendered_lines);
        const bool h_scroll_hovered =
            layout_state != nullptr &&
            layout_state->clickable.is_hovered(press_id::kEditorHorizontalScrollbar);
        const bool h_scroll_active =
            panel_state->h_scrollbar_dragging ||
            (layout_state != nullptr &&
             layout_state->clickable.is_pressed(press_id::kEditorHorizontalScrollbar));
        const int total_content_width = tabular_total_content_width(panel_state.get());
        Element code_column = attach_horizontal_scrollbar(
            std::move(code), total_content_width, buffer.scroll_col, code_width, h_scroll_hovered,
            h_scroll_active, panel_state.get());
        return hbox({gutter, separator() | color(theme::AccentDim()), std::move(code_column),
                     scrollbar}) |
               flex;
      }

      const int rendered_lines = static_cast<int>(code_rows.size());
      Element gutter = vbox(std::move(gutter_rows)) | reflect(panel_state->gutter_box) |
                       bgcolor(theme::CodeBg());
      Element code = vbox(std::move(code_rows)) | flex | reflect(panel_state->code_box) |
                     bgcolor(theme::CodeBg());
      Element scrollbar =
          vertical_scrollbar(1, 0, 1, rendered_lines, false, false) |
          reflect(panel_state->scrollbar_box);
      panel_state->scrollbar_layout = compute_scrollbar_layout(1, 0, 1, rendered_lines);
      panel_state->h_scrollbar_layout = {};
      return hbox({gutter, separator() | color(theme::AccentDim()), code | flex, scrollbar}) |
             flex;
    }

    const std::vector<TextMatch>* find_matches =
        find_state->open && !find_state->matches.empty() ? &find_state->matches : nullptr;
    const std::vector<TextMatch>* selection_occurrences =
        selection_occurrence_matches_for(panel_state.get(), buffer, find_state->open);

    static const BracketPairHighlight kNoBracket{};
    const BracketPairHighlight& bracket =
        typing_burst ? kNoBracket
                     : cached_bracket_highlight(panel_state.get(), buffer, editor_focused);
    static const BracketPairHighlight kNoScopeBracket{};
    const BracketPairHighlight& scope_bracket =
        typing_burst || !scope_visual_effects
            ? kNoScopeBracket
            : cached_scope_bracket_highlight(panel_state.get(), buffer, editor_focused);
    const BracketPairHighlight* scope_bracket_ptr =
        scope_bracket.valid ? &scope_bracket : nullptr;

    const std::vector<SymbolInfo>& file_symbols =
        cached_file_symbols(panel_state.get(), buffer.path, symbols.get());
    const std::vector<StickyLine> sticky_lines =
        defer_sticky_scroll
            ? std::vector<StickyLine>{}
            : (layout_state != nullptr && layout_state->app_settings != nullptr &&
                       layout_state->app_settings->sticky_scroll_enabled
                   ? sticky_lines_for_scroll(file_symbols, buffer.lines, buffer.scroll, 3)
                   : std::vector<StickyLine>{});

    DocumentDiagnostics file_diag;
    const bool allow_lsp_diagnostics_ui =
        layout_state == nullptr || layout_state->activity_gate.allows_lsp_ui();
    const bool deferred_sync = editor_deferred_sync_allowed(layout_state);
    const bool diagnostics_ready =
        symbols && symbols->supports_diagnostics() && !buffer.path.empty() &&
        is_lsp_trackable_path(buffer.path) &&
        diagnostics_display_allowed(workspace->last_buffer_edit_ms, symbols.get(), buffer.path,
                                    /*lsp_ui_allowed=*/true);
    const bool show_diagnostics = deferred_sync && diagnostics_ready && allow_lsp_diagnostics_ui;
    if (diagnostics_ready) {
      file_diag = cached_file_diagnostics(panel_state.get(), symbols.get(), buffer.path,
                                          workspace->last_buffer_edit_ms, layout_state);
      if (file_diag.path == buffer.path) {
        rebuild_diagnostics_by_line(panel_state.get(), file_diag,
                                    panel_state->cached_file_diag_revision);
      }
    } else {
      panel_state->diagnostics_by_line.clear();
      panel_state->diagnostics_by_line_path.clear();
      panel_state->diagnostics_by_line_revision = 0;
    }
    const bool has_git_markers =
        deferred_sync && git_service != nullptr && git_service->is_repo() &&
        (panel_state->git_untracked_all_lines || !panel_state->git_changed_lines.empty());
    const bool has_breakpoint_markers =
        debug_model != nullptr && !buffer.path.empty();
    const bool has_diagnostic_markers = show_diagnostics && !file_diag.items.empty();
    const bool gutter_markers =
        has_breakpoint_markers || has_diagnostic_markers || has_git_markers;

    const int code_width =
        code_width_from_box(panel_state->code_box, panel_state->code_width_chars);
    panel_state->code_width_chars = code_width;

    track_editor_scroll(panel_state.get(), buffer.scroll, layout_state);
    if (!typing_edit_mode) {
      rebuild_diagnostic_suffix_cache(panel_state.get(), buffer, code_width,
                                      panel_state->cached_file_diag_revision, symbols.get(),
                                      workspace->last_buffer_edit_ms, layout_state);
    }

    const bool suffixes_enabled =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->show_diagnostic_suffixes;
    const bool helix_caret =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->helix_mode_enabled;
    const Color cursor_cell =
        helix_caret && panel_state->helix.mode == HelixMode::kInsert ? theme::Success()
                                                                     : theme::CursorCell();
    const bool show_caret =
        !panel_state->mouse_selecting &&
        (!buffer.primary().has_selection() || helix_caret);
    const bool indent_guides_enabled =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
        layout_state->app_settings->indent_guides_enabled;
    const int indent_tab_size = std::max(1, editor_indent::width());
    const int tab_col_width = std::max(1, editor_indent::tab_display_width());
    {
      UiSyncPhaseScope guide_scope(ui_perf, "render.editor.guide");
      sync_guide_tracker_cache(panel_state.get(), buffer, buffer.scroll, tab_col_width);
    }
    IndentGuideTracker& guide_tracker = panel_state->guide_tracker_cache;

    if (buffer.scroll_col != panel_state->line_syntax_span_cache_scroll_col ||
        ts_highlight_revision != panel_state->line_syntax_span_cache_ts_revision) {
      panel_state->line_syntax_span_cache.clear();
      panel_state->line_syntax_span_cache_scroll_col = buffer.scroll_col;
      panel_state->line_syntax_span_cache_ts_revision = ts_highlight_revision;
    }
    if (ts_highlight_revision != panel_state->viewport_line_render_cache_ts_revision) {
      panel_state->viewport_line_render_cache.clear();
      panel_state->viewport_line_render_cache_ts_revision = ts_highlight_revision;
    }

    Elements gutter_rows;
    Elements code_rows;
    const std::string& buffer_source = editor_buffer_joined_source(buffer);
    SyntaxHighlightContext highlight_ctx;
    highlight_ctx.file_path = buffer.path;
    highlight_ctx.lines = &buffer.lines;
    highlight_ctx.joined_override = &buffer_source;
    highlight_ctx.buffer_token = buffer.view_token;
    highlight_ctx.ts_revision = ts_highlight_revision;
    highlight_ctx.semantic_revision = panel_state->last_semantic_highlight_revision;
    highlight_ctx.line_span_cache = &panel_state->line_syntax_span_cache;
    highlight_ctx.syntax_incremental = typing_burst;
    const ScopeLineRange immediate_scope =
        typing_burst ? ScopeLineRange{}
        : scope_visual_effects && !buffer.path.empty() && is_indexed_source_path(buffer.path)
            ? tree_sitter_service().innermost_scope_range_at(buffer.path, buffer.lines,
                                                             buffer.primary_line(),
                                                             buffer.primary_col())
            : ScopeLineRange{};
    static const std::vector<ColoredBraceMarker> kNoColoredBraces{};
    const std::vector<ColoredBraceMarker>& colored_braces =
        typing_burst ? kNoColoredBraces
                     : cached_colored_braces(panel_state.get(), buffer, scope_visual_effects);
    const std::vector<ColoredBraceMarker>* colored_braces_ptr =
        colored_braces.empty() ? nullptr : &colored_braces;
    const bool helix_caret_insert =
        helix_caret && panel_state->helix.mode == HelixMode::kInsert;
    const uint64_t semantic_revision = panel_state->last_semantic_highlight_revision;
    const int caret_line_index = buffer.primary_line();
    if (caret_line_index != panel_state->last_render_caret_line) {
      panel_state->viewport_line_render_cache.erase(panel_state->last_render_caret_line);
      panel_state->viewport_line_render_cache.erase(caret_line_index);
      panel_state->last_render_caret_line = caret_line_index;
    }
    {
      UiSyncPhaseScope lines_scope(ui_perf, "render.editor.lines");
      for (int i : viewport_lines) {
      const std::string& display_line = buffer.lines[static_cast<std::size_t>(i)];
      const int guide_depth =
          indent_guides_enabled ? guide_tracker.advance(display_line, tab_col_width) : 0;

      const bool is_primary = (i == buffer.primary_line());
      const bool caret_line = show_caret && editor_focused && is_primary;
      const bool in_immediate_scope_gutter =
          scope_visual_effects && immediate_scope.valid && immediate_scope.contains(i);

      const char fold_marker =
          fold_gutter_enabled
              ? fold_gutter_marker(i, buffer.fold_regions, buffer.collapsed_folds)
              : '\0';
      char gutter_marker = ' ';
      bool line_breakpoint = false;
      if (gutter_markers) {
        if (has_breakpoint_markers &&
            debug_model->has_breakpoint(normalize_path(buffer.path), i + 1)) {
          line_breakpoint = true;
          gutter_marker = '\0';
        } else {
          gutter_marker = line_gutter_marker(panel_state.get(), i);
        }
      }

      const std::string* suffix_ptr = nullptr;
      const std::vector<Diagnostic>* suffix_color_ptr = nullptr;
      const std::vector<Diagnostic>* line_diagnostics =
          diagnostics_for_editor_line(panel_state.get(), i);
      if (show_diagnostic_suffix_on_line(*panel_state, i, buffer, suffixes_enabled)) {
        const auto suffix_it = panel_state->diagnostic_suffix_by_line.find(i);
        if (suffix_it != panel_state->diagnostic_suffix_by_line.end()) {
          suffix_ptr = &suffix_it->second;
          suffix_color_ptr = line_diagnostics;
        }
      }

      EditorSymbolPress symbol_press;
      bool symbol_press_active = false;
      if (layout_state != nullptr && layout_state->editor_symbol_press.line == i) {
        const auto& press = layout_state->editor_symbol_press;
        if (editor_symbol_press_visible(layout_state)) {
          symbol_press = {press.start_col, press.end_col, true};
          symbol_press_active = true;
        }
      }

      ViewportLineRenderKeyInput key_input;
      key_input.line_index = i;
      key_input.line_content = display_line;
      key_input.guide_depth = guide_depth;
      key_input.fold_marker = fold_marker;
      key_input.gutter_marker = gutter_marker;
      key_input.has_breakpoint = line_breakpoint;
      key_input.in_immediate_scope_gutter = in_immediate_scope_gutter;
      key_input.suffix_ptr = suffix_ptr;
      key_input.symbol_press_active = symbol_press_active;
      key_input.line_diagnostics = line_diagnostics;
      key_input.find_matches = find_matches;
      key_input.selection_occurrences = selection_occurrences;
      const uint64_t render_key = compute_viewport_line_render_key(
          key_input, buffer, *panel_state, editor_focused, show_caret,
          panel_state->mouse_selecting, helix_caret_insert, buffer.scroll_col, code_width,
          helix_relative, gutter_w, fold_gutter_enabled, gutter_markers, indent_guides_enabled,
          scope_highlight_strength, typing_burst, bracket, scope_bracket_ptr,
          panel_state->colored_brace_cache_token, semantic_revision, semantic_source_generation,
          git_line_changed(panel_state.get(), i), ts_highlight_revision);

      auto cache_it = panel_state->viewport_line_render_cache.find(i);
      if (!caret_line && cache_it != panel_state->viewport_line_render_cache.end() &&
          cache_it->second.key == render_key) {
        gutter_rows.push_back(cache_it->second.gutter);
        code_rows.push_back(cache_it->second.code);
        continue;
      }

      const Decorator gutter_bg =
          is_primary ? bgcolor(theme::EditorLineHi())
                     : (in_immediate_scope_gutter
                            ? bgcolor(theme::ScopeBg(scope_highlight_strength))
                            : bgcolor(theme::CodeBg()));

      const Element fold_el =
          text(fold_marker == '\0' ? " " : std::string(1, fold_marker)) |
          color(fold_marker == '\0' ? theme::Muted() : theme::Accent());

      Element gutter_row;
      if (gutter_markers) {
        std::string gutter_text;
        if (line_breakpoint) {
          gutter_text = "●";
        } else {
          gutter_text.assign(1, gutter_marker == '\0' ? ' ' : gutter_marker);
        }
        gutter_text += helix_format_line_number(
            i, buffer.primary_line(), gutter_w - panel_state->gutter_fold_width, helix_relative);
        Color gutter_color = theme::Muted();
        if (line_breakpoint) {
          gutter_color = Color::Red;
        } else if (gutter_marker == '!') {
          gutter_color = theme::Error();
        } else if (gutter_marker == 'G') {
          gutter_color = theme::Success();
        } else if (gutter_marker == 'W') {
          gutter_color = theme::Warning();
        }
        gutter_row = hbox({fold_el, text(gutter_text) | color(gutter_color)}) | gutter_bg;
      } else {
        gutter_row =
            hbox({fold_el,
                  text(helix_format_line_number(i, buffer.primary_line(),
                                                gutter_w - panel_state->gutter_fold_width,
                                                helix_relative)) |
                      color(theme::Muted())}) |
            gutter_bg;
      }

      const SemanticTokenDocument* line_semantic =
          typing_burst && !caret_line ? nullptr : semantic_tokens;
      Element code_row =
          RenderEditorLine(display_line, i, buffer, editor_focused, find_matches,
                           selection_occurrences, line_semantic,
                           bracket.valid ? &bracket : nullptr, scope_bracket_ptr,
                           scope_highlight_strength, line_diagnostics, suffix_ptr,
                           suffix_color_ptr, symbol_press_active ? &symbol_press : nullptr,
                           show_caret, buffer.scroll_col, code_width, &highlight_ctx, false,
                           indent_guides_enabled, guide_depth, false, cursor_cell,
                           colored_braces_ptr) |
          xflex_shrink;

      CachedViewportLineRow cached_row;
      cached_row.key = render_key;
      cached_row.gutter = gutter_row;
      cached_row.code = code_row;
      panel_state->viewport_line_render_cache[i] = std::move(cached_row);
      gutter_rows.push_back(panel_state->viewport_line_render_cache[i].gutter);
      code_rows.push_back(panel_state->viewport_line_render_cache[i].code);
    }
    }
    prune_viewport_line_render_cache(panel_state.get(), viewport_lines);
    if (code_rows.empty()) {
      gutter_rows.push_back(text(format_line_number(1, gutter_w)) | color(theme::Muted()) |
                            bgcolor(theme::CodeBg()));
      code_rows.push_back(text(" ") | bgcolor(theme::CodeBg()));
    }

    const int rendered_lines = static_cast<int>(code_rows.size());

    Element gutter = vbox(std::move(gutter_rows)) | reflect(panel_state->gutter_box) |
                     bgcolor(theme::CodeBg());
    Element code = vbox(std::move(code_rows)) | flex | reflect(panel_state->code_box) |
                   bgcolor(theme::CodeBg());
    const bool scroll_hovered =
        layout_state != nullptr && layout_state->clickable.is_hovered(press_id::kEditorScrollbar);
    const bool scroll_active =
        panel_state->scrollbar_dragging ||
        (layout_state != nullptr &&
         layout_state->clickable.is_pressed(press_id::kEditorScrollbar));
    const bool overview_ruler_enabled =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->overview_ruler_enabled;
    Element scrollbar =
        vertical_scrollbar(scroll_total, scroll_visible_index, visible, rendered_lines,
                           scroll_hovered, scroll_active) |
        reflect(panel_state->scrollbar_box);
    panel_state->scrollbar_layout =
        compute_scrollbar_layout(scroll_total, scroll_visible_index, visible, rendered_lines);

    const bool h_scroll_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(press_id::kEditorHorizontalScrollbar);
    const bool h_scroll_active =
        panel_state->h_scrollbar_dragging ||
        (layout_state != nullptr &&
         layout_state->clickable.is_pressed(press_id::kEditorHorizontalScrollbar));
    const int total_content_width = editor_horizontal_content_width(buffer, code_width);
    Element code_column = attach_horizontal_scrollbar(
        std::move(code), total_content_width, buffer.scroll_col, code_width, h_scroll_hovered,
        h_scroll_active, panel_state.get());

    Elements editor_columns = {gutter, separator() | color(theme::AccentDim()),
                               std::move(code_column)};
    if (overview_ruler_enabled) {
      OverviewRulerInput ruler_input;
      ruler_input.total_lines = total;
      ruler_input.scroll = buffer.scroll;
      ruler_input.visible_lines = visible;
      ruler_input.diagnostics_by_line = &panel_state->diagnostics_by_line;
      ruler_input.git_changed_lines = &panel_state->git_changed_lines;
      ruler_input.git_untracked_all = panel_state->git_untracked_all_lines;
      if (find_state->open && !find_state->matches.empty()) {
        ruler_input.text_matches = &find_state->matches;
      } else {
        ruler_input.text_matches =
            selection_occurrence_matches_for(panel_state.get(), buffer, find_state->open);
      }
      Element overview_ruler =
          vertical_overview_ruler(ruler_input, rendered_lines,
                                  &panel_state->overview_ruler_layout) |
          reflect(panel_state->overview_ruler_box);
      editor_columns.push_back(std::move(overview_ruler));
    } else {
      panel_state->overview_ruler_layout = {};
    }
    editor_columns.push_back(std::move(scrollbar));
    Element editor_body = hbox(std::move(editor_columns));

    Element body = std::move(editor_body) | frame | flex | bgcolor(theme::CodeBg());
    if (sticky_lines.empty()) {
      return body;
    }
    return dbox({std::move(body),
                 make_sticky_overlay(sticky_lines, gutter_w, buffer, semantic_tokens, code_width,
                                     indent_guides_enabled)});
  });

  // En Stacked, el primer hijo se dibuja encima (FTXUI invierte el dbox interno).
  auto editor_stack = Container::Stacked({
      modal_overlay,
      code_view | flex,
  });

  // Sin Container::Vertical+Maybe: con muchas líneas el layout DOM de FTXUI se bloqueaba.
  auto panel = Renderer(editor_stack, [workspace, find_state, editor_stack, panel_state,
                                       tab_bar_state, symbols, layout_state] {
    workspace->ensure_buffer();
    if (tab_bar_state->bar_box.x_max > tab_bar_state->bar_box.x_min) {
      tab_bar_state->bar_width_chars =
          tab_bar_state->bar_box.x_max - tab_bar_state->bar_box.x_min + 1;
    }
    const EditorBuffer& buffer = workspace->buffer;
    std::string file_label = "Editor";
    if (!buffer.path.empty()) {
      file_label = std::filesystem::path(buffer.path).filename().string();
    }
    const std::vector<SymbolInfo>& file_symbols =
        cached_file_symbols(panel_state.get(), buffer.path, symbols.get());
    const std::vector<BreadcrumbItem> crumbs =
        build_breadcrumbs(file_label, file_symbols, buffer.primary_line());
    Element title = make_breadcrumb_bar(crumbs, buffer, *find_state, panel_state.get(), symbols,
                                        layout_state);
    Element tab_bar = make_editor_tab_bar(workspace, tab_bar_state.get(), layout_state);
    Element editor = editor_stack->Render() | flex;
    Element chrome =
        vbox({std::move(tab_bar), std::move(title), PanelBody(std::move(editor), theme::CodeBg())});
    Element tooltip = make_tab_hover_tooltip(workspace, tab_bar_state.get());
    return dbox({std::move(chrome), std::move(tooltip)}) | flex | bgcolor(theme::CodeBg());
  });

  auto dispatch_editor_keys = [workspace, focus, panel_state, tab_bar_state, find_state,
                               goto_state, completion_state, diagnostic_state, git_history_state,
                               symbols, file_indexer, symbol_indexer, layout_state, debug_model,
                               on_command, panel_focus](Event event) {
    if (tab_bar_state->overflow_open &&
        handle_tabs_overflow_keys(workspace, focus, tab_bar_state.get(), event, panel_focus)) {
      return true;
    }
    if (layout_state != nullptr &&
        layout_state->text_input_focus == TextInputFocus::Console) {
      return false;
    }
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    const int visible = visible_line_count(panel_state->code_box);

    if (completion_state->open && event == Event::Escape) {
      completion_state->close(layout_state);
      if (focus != nullptr) {
        focus->region = panel_focus;
      }
      if (layout_state != nullptr) {
        layout_state->editor_completion_open = false;
      }
      return true;
    }

    if (find_input_active(layout_state, *find_state) &&
        handle_find_keys(find_state.get(), layout_state, buffer, event, visible)) {
      if (focus != nullptr) {
        focus->region = panel_focus;
      }
      return true;
    }
    if (focus->region != panel_focus) {
      return false;
    }
    if (event_is_ctrl_f(event)) {
      activate_find(find_state.get(), buffer, layout_state, focus, panel_focus);
      return true;
    }
    const bool handled =
        handle_editor_keys(workspace, focus, find_state.get(), goto_state.get(),
                           completion_state.get(), panel_state.get(), diagnostic_state.get(),
                           git_history_state.get(), symbols, file_indexer, symbol_indexer,
                           layout_state, debug_model, on_command, event, visible);
    if (handled && layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return handled;
  };

  auto dispatch_editor_chrome_mouse = [workspace, focus, panel_state, tab_bar_state,
                                       layout_state](Event event) {
    workspace->ensure_buffer();
    const int visible = visible_line_count(panel_state->code_box);
    return handle_editor_chrome_mouse(workspace, focus, panel_state.get(), tab_bar_state.get(),
                                      layout_state, event, visible);
  };

  auto dispatch_editor_mouse = [workspace, focus, panel_state, find_state, completion_state,
                                  diagnostic_state, git_history_state, git_service, layout_state,
                                  symbols, debug_model, on_command, dispatch_editor_chrome_mouse,
                                  panel_focus](Event event) {
    if (dispatch_editor_chrome_mouse(event)) {
      return true;
    }

    if (event.is_mouse()) {
      const auto& m = event.mouse();
      if (!editor_mouse_targets_code(*panel_state, m.x, m.y)) {
        return false;
      }
      workspace->ensure_buffer();
      const int visible = visible_line_count(panel_state->code_box);
      return handle_editor_mouse(workspace, focus, find_state.get(), completion_state.get(),
                                 layout_state,
                                 panel_state.get(), diagnostic_state.get(),
                                 git_history_state.get(), git_service, symbols, debug_model,
                                 on_command, event, visible);
    }

    if (layout_state != nullptr &&
        layout_state->text_input_focus == TextInputFocus::Console) {
      return false;
    }
    if (focus->region != panel_focus) {
      return false;
    }
    workspace->ensure_buffer();
    const int visible = visible_line_count(panel_state->code_box);
    return handle_editor_mouse(workspace, focus, find_state.get(), completion_state.get(),
                               layout_state,
                               panel_state.get(), diagnostic_state.get(),
                               git_history_state.get(), git_service, symbols, debug_model,
                               on_command, event, visible);
  };

  auto dispatch_editor_modifiers = [layout_state](Event& event) {
    if (layout_state == nullptr) {
      return;
    }
    if (event_is_ctrl_key_press(event)) {
      layout_state->editor_ctrl_modifier_held = true;
    } else if (event_is_ctrl_key_release(event)) {
      layout_state->editor_ctrl_modifier_held = false;
    }
  };

  if (handlers != nullptr) {
    handlers->key_handler = dispatch_editor_keys;
    handlers->mouse_handler = dispatch_editor_mouse;
    handlers->chrome_mouse_handler = dispatch_editor_chrome_mouse;
    handlers->modifier_handler = dispatch_editor_modifiers;
    handlers->visible_line_count = [panel_state]() {
      return visible_line_count(panel_state->code_box);
    };
  }

  if (layout_state != nullptr) {
    EditorPanelState* panel_ptr = panel_state.get();
    const auto previous_reset = layout_state->reset_helix_editors;
    layout_state->reset_helix_editors = [previous_reset, panel_ptr, layout_state]() {
      if (previous_reset) {
        previous_reset();
      }
      reset_helix_editor_state(&panel_ptr->helix);
      const bool enabled = layout_state->app_settings != nullptr &&
                           layout_state->app_settings->helix_mode_enabled;
      sync_helix_layout_status(layout_state, &panel_ptr->helix, enabled);
    };
  }

  if (handlers != nullptr) {
    handlers->tick_callback = [workspace, panel_state, find_state, symbols, layout_state,
                               file_indexer, git_service, focus, panel_focus,
                               completion_state]() {
      TGDB_MON_SCOPE("editor", "tick_callback");
      UiPerfMonitor* ui_perf =
          layout_state != nullptr ? &layout_state->ui_perf_monitor : nullptr;
      const std::string panel_tag =
          panel_focus == FocusRegion::SecondaryEditor ? "sec" : "pri";
      if (panel_focus == FocusRegion::SecondaryEditor && workspace->tabs.empty() &&
          focus != nullptr && focus->region == FocusRegion::SecondaryEditor) {
        focus->region = FocusRegion::Editor;
        if (layout_state != nullptr) {
          layout_state->focus_sync_needed = true;
        }
      }
      const int visible = visible_line_count(panel_state->code_box);
      {
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".nav");
        tick_pending_editor_navigation(layout_state, [&](const SourceLocation& loc) {
          navigate_to_location(workspace, layout_state, loc, visible);
        });
      }
      {
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".hover");
        editor_hover_tick(workspace, panel_state.get(), symbols, layout_state);
      }
      {
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".completion");
        completion_lsp_tick(completion_state.get(), workspace, symbols, layout_state,
                            panel_state.get());
      }
      workspace->ensure_buffer();

      const int64_t now = steady_now_ms();
      if (now - panel_state->last_heavy_editor_tick_ms < kHeavyEditorTickIntervalMs) {
        return;
      }
      panel_state->last_heavy_editor_tick_ms = now;

      UiSyncPhaseScope heavy_scope(ui_perf, "tick." + panel_tag + ".heavy");
      {
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.lsp_sync");
        tick_lsp_buffer_sync(panel_state.get(), workspace, symbols, layout_state);
      }
      {
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.selection");
        tick_selection_occurrence_matches(panel_state.get(), workspace->buffer, layout_state);
      }
      if (find_state->tick_matches(workspace->buffer) && layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      if (symbols && workspace != nullptr) {
        const std::string& path = workspace->buffer.path;
        if (!path.empty()) {
          if (panel_state->document_open_pending && symbols) {
            panel_state->document_open_pending = false;
            if (!is_tabular_path(panel_state->pending_document_open_path)) {
              symbols->on_document_opened(panel_state->pending_document_open_path,
                                          buffer_text(workspace->buffer));
            }
          }
          {
            UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.lsp_debounce");
            symbols->tick_debounced_updates();
          }
          if (panel_state->symbols_fetch_pending) {
            UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.symbols");
            ensure_file_symbols(panel_state.get(), symbols.get(), path,
                                buffer_text(workspace->buffer));
          }
          const uint64_t sym_rev = symbols->document_symbols_revision();
          if (sym_rev != panel_state->last_document_symbols_revision) {
            panel_state->last_document_symbols_revision = sym_rev;
            if (!is_tabular_path(path)) {
              panel_state->symbols_fetch_pending = true;
            }
            UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.symbols");
            ensure_file_symbols(panel_state.get(), symbols.get(), path,
                                buffer_text(workspace->buffer));
          }
          if (symbols->supports_semantic_highlight() && !path.empty() &&
              !is_tabular_path(path)) {
            const uint64_t sem_rev = symbols->semantic_highlight_revision();
            if (sem_rev != panel_state->last_semantic_highlight_revision_tick) {
              panel_state->last_semantic_highlight_revision_tick = sem_rev;
              if (!symbols->semantic_tokens_current_for_file(path)) {
                panel_state->semantic_tokens_enqueue_pending = true;
              }
            }
            if (panel_state->semantic_tokens_enqueue_pending &&
                (!panel_state->semantic_tokens_layout_stale ||
                 editor_content_settled(*panel_state))) {
              UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.semantic");
              const bool ready = symbols->ensure_semantic_tokens(path);
              if (ready && symbols->semantic_tokens_current_for_file(path)) {
                panel_state->semantic_tokens_enqueue_pending = false;
              } else if (!symbols->lsp_loading() && !symbols->supports_semantic_highlight()) {
                panel_state->semantic_tokens_enqueue_pending = false;
              }
            }
          }
          {
            UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.diagnostics");
            if (editor_deferred_sync_allowed(layout_state)) {
              sync_diagnostic_cache(panel_state.get(), symbols.get(), workspace, file_indexer,
                                    layout_state);
            }
          }
        }
      }
      if (git_service != nullptr && git_service->is_repo() &&
          editor_deferred_sync_allowed(layout_state)) {
        workspace->ensure_buffer();
        const EditorBuffer& buf = workspace->buffer;
        UiSyncPhaseScope scope(ui_perf, "tick." + panel_tag + ".heavy.git");
        sync_git_cache(panel_state.get(), git_service, buf);
      }
    };
  }

  return WrapFocusable(CatchEvent(panel, [dispatch_editor_keys, dispatch_editor_mouse, workspace,
                                          focus, panel_state, tab_bar_state, find_state,
                                          layout_state, symbols, panel_focus](Event event) {
    if (tab_bar_state->overflow_open &&
        handle_tabs_overflow_keys(workspace, focus, tab_bar_state.get(), event, panel_focus)) {
      return true;
    }
    if (dispatch_editor_mouse(event)) {
      return true;
    }
    return dispatch_editor_keys(event);
  }));
}

}  // namespace tgdb
