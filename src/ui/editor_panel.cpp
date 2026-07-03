#include "ui/editor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "editor/bracket_match.hpp"
#include "editor/clipboard.hpp"
#include "git/git_service.hpp"
#include "editor/editor_context.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/editor_render.hpp"
#include "editor/selection_occurrence_runner.hpp"
#include "util/cpp_highlight.hpp"
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
#include "symbols/local_scope_completions.hpp"
#include "symbols/symbol_utils.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/clickable.hpp"
#include "ui/editor_tab_bar.hpp"
#include "ui/press_ids.hpp"
#include "ui/focusable_component.hpp"
#include "ui/context_menu.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
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

// #region agent log
std::string editor_debug_bytes_hex(const std::string& bytes) {
  std::ostringstream oss;
  for (unsigned char byte : bytes) {
    if (oss.tellp() > 0) {
      oss << ' ';
    }
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
  }
  return oss.str();
}

void editor_debug_agent_log(const char* hypothesis_id, const char* location, const char* message,
                            const std::string& data_json) {
  std::ofstream out("/home/lorenzo/workspace/tgdb/.cursor/debug-4e0960.log", std::ios::app);
  if (!out) {
    return;
  }
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  out << "{\"sessionId\":\"4e0960\",\"hypothesisId\":\"" << hypothesis_id
      << "\",\"location\":\"" << location << "\",\"message\":\"" << message << "\",\"data\":"
      << data_json << ",\"timestamp\":" << timestamp << "}\n";
}
// #endregion

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

void mark_editor_content_edited(EditorPanelState* panel, const EditorBuffer& buffer);

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
  buffer.view_token++;
  if (panel != nullptr) {
    mark_editor_content_edited(panel, buffer);
  } else if (symbols != nullptr) {
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

struct EditorPanelState {
  FocusRegion panel_focus = FocusRegion::Editor;
  Box code_box;
  Box gutter_box;
  Box breadcrumb_box;
  Box problems_button_box;
  Box scrollbar_box;
  Box overview_ruler_box;
  ScrollbarLayout scrollbar_layout;
  OverviewRulerLayout overview_ruler_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  uint64_t last_view_token = 0;
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
  uint64_t last_diag_revision = 0;
  std::string last_diag_path;
  int problem_errors = 0;
  int problem_warnings = 0;
  DocumentDiagnostics cached_file_diag;
  uint64_t cached_file_diag_revision = 0;
  int gutter_scroll_start = 0;
  int gutter_visible_rows = 0;
  bool symbols_fetch_pending = false;
  uint64_t last_document_symbols_revision = 0;
  bool semantic_tokens_enqueue_pending = false;
  uint64_t last_semantic_highlight_revision_tick = 0;
  SemanticTokenDocument cached_semantic_tokens;
  std::string cached_semantic_path;
  uint64_t last_semantic_highlight_revision = 0;
  int code_width_chars = 80;
  std::vector<bool> block_comment_line_starts;
  uint64_t block_comment_token = 0;
  std::size_t block_comment_line_count = 0;
  int block_comment_dirty_from_line = -1;
  int64_t content_edit_ms = 0;
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
  std::string git_cache_path;
  uint64_t git_cache_revision = 0;
  int last_render_scroll = 0;
  int64_t last_scroll_change_ms = 0;
  bool document_open_pending = false;
  std::string pending_document_open_path;
  std::vector<TextMatch> selection_occurrence_matches;
  SelectionOccurrenceKey selection_occurrence_committed_key;
  SelectionOccurrenceRunner selection_occurrence_runner;
  uint64_t selection_occurrence_request_counter = 0;
  uint64_t selection_occurrence_inflight_id = 0;
  struct SourceSymbolFlash {
    int line = -1;
    int start_col = 0;
    int end_col = 0;
    std::chrono::steady_clock::time_point until{};

    bool active() const {
      return line >= 0 && end_col > start_col &&
             std::chrono::steady_clock::now() < until;
    }

    void clear() { line = -1; }
  };
  SourceSymbolFlash source_flash;
  bool chord_k_pending = false;
};

void flash_symbol_at_buffer_pos_impl(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                     EditorPanelState* panel_state, int line, int col,
                                     int visible_lines);

constexpr int kDoubleClickMs = 400;
constexpr int kHoverDelayMs = 500;
constexpr int kSuffixScrollSettleMs = 150;
constexpr int kEditorContentSettleMs = 120;

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void mark_editor_content_edited(EditorPanelState* panel, const EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }
  panel->content_edit_ms = steady_now_ms();
  const int line = buffer.primary_line();
  if (panel->block_comment_dirty_from_line < 0 || line < panel->block_comment_dirty_from_line) {
    panel->block_comment_dirty_from_line = line;
  }
  panel->lsp_sync_pending = true;
}

bool editor_content_settled(const EditorPanelState& panel) {
  return steady_now_ms() - panel.content_edit_ms >= kEditorContentSettleMs;
}

CursorPos mouse_to_cursor(const Mouse& m, const EditorPanelState& panel, const EditorBuffer& buffer,
                          int visible_lines);
void end_mouse_selection(EditorPanelState* panel);

void clear_hover_state(EditorHoverState* hover) {
  if (hover == nullptr) {
    return;
  }
  hover->visible = false;
  hover->line = -1;
  hover->col = -1;
  hover->fetch_key.clear();
  hover->info = {};
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

void rebuild_block_comment_cache(EditorPanelState* panel, const EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }
  const std::size_t line_count = buffer.lines.size();
  if (panel->block_comment_token == buffer.view_token &&
      panel->block_comment_line_count == line_count) {
    return;
  }

  const bool can_incremental =
      line_count == panel->block_comment_line_count && panel->block_comment_dirty_from_line >= 0 &&
      static_cast<std::size_t>(panel->block_comment_dirty_from_line) < line_count &&
      panel->block_comment_line_starts.size() == line_count;

  std::size_t start_line = 0;
  CppHighlightContext ctx;
  if (can_incremental) {
    start_line = static_cast<std::size_t>(panel->block_comment_dirty_from_line);
    ctx.in_block_comment = panel->block_comment_line_starts[start_line];
  } else {
    panel->block_comment_line_starts.assign(line_count, false);
  }

  for (std::size_t i = start_line; i < line_count; ++i) {
    panel->block_comment_line_starts[i] = ctx.in_block_comment;
    advance_cpp_highlight_context(buffer.lines[i], &ctx);
  }
  panel->block_comment_token = buffer.view_token;
  panel->block_comment_line_count = line_count;
  panel->block_comment_dirty_from_line = -1;
}

void tick_block_comment_cache(EditorPanelState* panel, const EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }
  if (panel->block_comment_token == buffer.view_token &&
      panel->block_comment_line_count == buffer.lines.size()) {
    return;
  }
  if (!editor_content_settled(*panel)) {
    return;
  }
  rebuild_block_comment_cache(panel, buffer);
}

void tick_lsp_buffer_sync(EditorPanelState* panel, WorkspaceModel* workspace,
                          const std::shared_ptr<ISymbolProvider>& symbols) {
  if (panel == nullptr || workspace == nullptr || symbols == nullptr || !panel->lsp_sync_pending) {
    return;
  }
  if (!editor_content_settled(*panel)) {
    return;
  }
  workspace->ensure_buffer();
  const EditorBuffer& buffer = workspace->buffer;
  if (buffer.path.empty()) {
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
    panel->symbols_fetch_pending = !path.empty() && symbols != nullptr;
  }
  return panel->cached_symbols;
}

void ensure_file_symbols(EditorPanelState* panel, ISymbolProvider* symbols,
                         const std::string& path) {
  if (panel == nullptr || symbols == nullptr || path.empty() || !panel->symbols_fetch_pending) {
    return;
  }
  panel->cached_symbols = symbols->symbols_for_file(path);
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
                                     int code_width, uint64_t revision) {
  if (panel == nullptr || code_width <= 0) {
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
  return panel != nullptr && panel->git_changed_lines.count(line) > 0;
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
  panel->git_changed_lines.clear();
  panel->git_previous_by_line.clear();
  if (diff.untracked) {
    for (std::size_t i = 0; i < buffer_lines.size(); ++i) {
      panel->git_changed_lines.insert(static_cast<int>(i));
    }
    return;
  }
  for (const auto& [line_no, change] : diff.line_changes) {
    (void)change;
    panel->git_changed_lines.insert(line_no);
  }
  for (const auto& [line_no, content] : diff.previous_content_by_line) {
    panel->git_previous_by_line[line_no] = content;
  }
}

void sync_git_cache(EditorPanelState* panel, GitService* git, const EditorBuffer& buffer) {
  if (panel == nullptr || git == nullptr || !git->is_repo() || buffer.path.empty()) {
    if (panel != nullptr) {
      panel->git_changed_lines.clear();
      panel->git_previous_by_line.clear();
      panel->git_cache_path.clear();
      panel->git_cache_revision = 0;
    }
    return;
  }

  const uint64_t revision = git->cache_revision();
  if (buffer.path != panel->git_cache_path) {
    panel->git_cache_path = buffer.path;
    panel->git_cache_revision = 0;
    panel->git_changed_lines.clear();
    panel->git_previous_by_line.clear();
    git->refresh_file_diff(buffer.path);
  }

  if (revision != panel->git_cache_revision) {
    panel->git_cache_revision = revision;
    if (!git->has_file_diff_text(buffer.path)) {
      git->refresh_file_diff(buffer.path);
    }
  }

  apply_git_diff_to_panel(panel, git->file_diff(buffer.path), buffer.lines);
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

SelectionOccurrenceKey selection_occurrence_key_from(const EditorBuffer& buffer) {
  SelectionOccurrenceKey key;
  key.path = buffer.path;
  key.view_token = buffer.view_token;
  if (buffer.cursors.size() == 1 && buffer.primary().has_selection()) {
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
                           WorkspaceModel* workspace, WorkspaceIndexer* indexer) {
  if (panel == nullptr || symbols == nullptr || !symbols->supports_diagnostics()) {
    return;
  }
  const uint64_t revision = symbols->diagnostics_revision();
  const std::string path =
      workspace != nullptr && !workspace->buffer.path.empty() ? workspace->buffer.path
                                                                : std::string{};
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
                                                   const std::string& path) {
  static const DocumentDiagnostics kEmpty;
  if (panel == nullptr || symbols == nullptr || !symbols->supports_diagnostics() ||
      path.empty() || !is_lsp_trackable_path(path)) {
    return kEmpty;
  }
  const uint64_t revision = symbols->diagnostics_revision();
  if (revision != panel->cached_file_diag_revision || panel->cached_file_diag.path != path) {
    panel->cached_file_diag = symbols->diagnostics_for_file(path);
    panel->cached_file_diag_revision = revision;
  }
  return panel->cached_file_diag;
}

void editor_hover_tick(WorkspaceModel* workspace, EditorPanelState* panel,
                       const std::shared_ptr<ISymbolProvider>& symbols) {
  if (workspace == nullptr || panel == nullptr) {
    return;
  }
  auto& hover = panel->hover;
  if (hover.line < 0 || hover.visible) {
    return;
  }
  const int64_t now_ms = steady_now_ms();
  if (now_ms - hover.dwell_start_ms < kHoverDelayMs) {
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
      return;
    }
    if (symbols->hover_uses_async_fetch()) {
      if (const auto polled = symbols->poll_hover(key)) {
        hover.info = *polled;
        hover.visible = polled->valid;
      }
    }
    return;
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
}

void track_hover_mouse(EditorPanelState* panel, const Mouse& m, const EditorBuffer& buffer,
                       int visible_lines) {
  if (panel == nullptr) {
    return;
  }
  const CursorPos pos = mouse_to_cursor(m, *panel, buffer, visible_lines);
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
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
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
  const int new_scroll = scroll_for_thumb_top(panel->scrollbar_layout, thumb_top);
  if (buffer->scroll != new_scroll) {
    buffer->scroll = new_scroll;
    clear_hover_state(&panel->hover);
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
    if (layout_state != nullptr) {
      const std::string_view before = layout_state->clickable.hovered_id();
      if (in_bar || panel->scrollbar_dragging) {
        layout_state->clickable.set_hover(press_id::kEditorScrollbar);
      } else {
        layout_state->clickable.clear_hover_if([&](std::string_view id) {
          return id == press_id::kEditorScrollbar;
        });
      }
      if (layout_state->clickable.hovered_id() != before) {
        layout_state->request_ui_tick = true;
      }
    }
    if (panel->scrollbar_dragging) {
      return apply_scrollbar_drag(workspace, panel, m.y, visible_lines);
    }
    return in_bar;
  }

  if (panel->scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      panel->scrollbar_dragging = false;
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
    workspace->ensure_buffer();
    scroll_view_by_lines(&workspace->buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
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
      workspace->buffer.scroll =
          scroll_for_thumb_top(panel->scrollbar_layout, thumb_top);
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

bool handle_editor_chrome_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                                EditorPanelState* panel, EditorTabBarState* tab_bar,
                                MainLayoutState* layout_state, Event event, int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }
  const auto& m = event.mouse();
  if (m.motion == Mouse::Moved) {
    update_editor_chrome_hover(workspace, tab_bar, layout_state, panel->problems_button_box, m.x,
                               m.y);
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

  workspace->ensure_buffer();
  const bool in_code = panel->code_box.Contain(m.x, m.y);
  if (m.motion == Mouse::Moved) {
    if (in_code && !panel->mouse_selecting) {
      track_hover_mouse(panel, m, workspace->buffer, visible_lines);
    } else if (!in_code && !panel->breadcrumb_box.Contain(m.x, m.y) &&
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
  rows.push_back(text(" Línea " + std::to_string(state.line + 1)) | color(theme::Success()) | bold);
  rows.push_back(separator() | color(theme::AccentDim()));
  rows.push_back(text(" Antes (HEAD):") | color(theme::Muted()) | bold);
  rows.push_back(paragraphAlignLeft(" " + (state.previous_content.empty() ? "(vacío)"
                                                                    : state.previous_content)) |
                 color(theme::Header()));
  rows.push_back(text(""));
  rows.push_back(text(" Ahora:") | color(theme::Muted()) | bold);
  rows.push_back(paragraphAlignLeft(" " + (state.current_content.empty() ? "(vacío)"
                                                                     : state.current_content)) |
                 color(theme::Header()));
  rows.push_back(text(""));
  rows.push_back(text(" Esc cerrar") | color(theme::Muted()));

  Element dialog = ModalWindow(
      text("Historial Git") | color(theme::Success()),
      vbox(std::move(rows)) | size(WIDTH, GREATER_THAN, 40) | size(HEIGHT, LESS_THAN, 18));
  return CenteredModal(std::move(dialog));
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
  const int line = panel->gutter_scroll_start + row;
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
  const int line = panel->gutter_scroll_start + row;
  const char marker = line_gutter_marker(panel, line);
  if (marker == '\0' || marker == 'G') {
    return false;
  }
  modal->open = true;
  modal->line = line;
  modal->items = diagnostics_on_line(file_diag, line);
  return true;
}

Element make_diagnostic_modal(const DiagnosticModalState& state) {
  if (!state.open || state.items.empty()) {
    return text("");
  }

  Elements rows;
  rows.push_back(text(" Línea " + std::to_string(state.line + 1)) | color(theme::Accent()) | bold);
  rows.push_back(separator() | color(theme::AccentDim()));

  for (const auto& item : state.items) {
    std::string header = diagnostic_severity_label(item.severity);
    if (!item.source.empty()) {
      header += " [" + item.source + "]";
    }
    if (item.start_col >= 0) {
      header += "  col " + std::to_string(item.start_col + 1);
      if (item.end_col > item.start_col) {
        header += "-" + std::to_string(item.end_col);
      }
    }
    rows.push_back(text(" " + header) | color(diagnostic_severity_color(item.severity)) | bold);
    rows.push_back(paragraphAlignLeft(" " + item.message) | color(theme::Header()));
    rows.push_back(text(""));
  }
  rows.push_back(text(" Esc cerrar") | color(theme::Muted()));

  Element dialog = ModalWindow(
      text("Diagnóstico") | color(theme::Accent()),
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
      segments.push_back(text(" › ") | color(theme::Muted()));
    }
  }

  std::string meta;
  meta += "  L" + std::to_string(buffer.primary_line() + 1) + ":" +
          std::to_string(buffer.primary_col() + 1);
  if (buffer.dirty) {
    meta += " *";
  }
  if (buffer.multi_cursor_active()) {
    meta += "  [" + std::to_string(buffer.cursors.size()) + " cursores]";
  }
  if (find.open) {
    meta += "  [buscar]";
  }

  int problem_errors = 0;
  int problem_warnings = 0;
  if (panel_state != nullptr) {
    problem_errors = panel_state->problem_errors;
    problem_warnings = panel_state->problem_warnings;
  }
  std::string problems_label = " Problemas";
  if (problem_errors > 0) {
    problems_label += " (" + std::to_string(problem_errors) + ")";
  } else if (problem_warnings > 0) {
    problems_label += " (" + std::to_string(problem_warnings) + "w)";
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
                            const std::vector<bool>& block_comment_line_starts) {
  if (sticky_lines.empty()) {
    return text("");
  }
  Elements rows;
  for (const StickyLine& sticky : sticky_lines) {
    const std::string indent(static_cast<std::size_t>(sticky.depth * 2), ' ');
    const std::string& display_line =
        buffer.lines[static_cast<std::size_t>(sticky.source_line)];

    CppHighlightContext highlight_ctx;
    if (sticky.source_line >= 0 &&
        sticky.source_line < static_cast<int>(block_comment_line_starts.size())) {
      highlight_ctx.in_block_comment =
          block_comment_line_starts[static_cast<std::size_t>(sticky.source_line)];
    }

    Element line = RenderEditorLine(display_line, sticky.source_line, buffer, false, nullptr, nullptr,
                                  semantic_tokens, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  false, 0, code_width, &highlight_ctx, true);
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

struct CompletionState {
  bool open = false;
  bool live_mode = false;
  bool semantic_mode = false;
  bool workspace_index = false;
  bool index_scanning = false;
  std::string prefix;
  std::string query;
  std::string fetch_key;
  std::vector<CompletionItem> all_items;
  std::vector<CompletionItem> matches;
  int selected = 0;
  int replace_line = 0;
  int replace_start = 0;
  int replace_end = 0;

  static std::string to_lower(std::string value) {
    for (char& c : value) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
  }

  static bool prefix_match(const std::string& name, const std::string& query) {
    if (query.empty()) {
      return true;
    }
    const std::string n = to_lower(name);
    const std::string q = to_lower(query);
    return n.size() >= q.size() && n.compare(0, q.size(), q) == 0;
  }

  static CompletionItem from_symbol(const SymbolInfo& sym) {
    CompletionItem item;
    item.label = sym.name;
    item.kind = sym.kind;
    item.file = sym.file;
    return item;
  }

  static CompletionItem from_indexed(const IndexedSymbol& sym) {
    CompletionItem item;
    item.label = sym.display_name;
    item.kind = sym.kind;
    item.file = sym.file;
    return item;
  }

  void merge_local_scope_items(WorkspaceModel* workspace) {
    if (workspace == nullptr) {
      return;
    }
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    if (path.empty()) {
      return;
    }
    workspace->buffer.ensure_cursors();
    CompletionParams params;
    params.path = path;
    params.text = buffer_text(workspace->buffer);
    params.line = workspace->buffer.primary_line();
    params.character = workspace->buffer.primary_col();
    merge_completion_items(&all_items,
                           local_scope_completions(params.text, params.line, params.character));
  }

  void refresh_matches() {
    matches.clear();
    constexpr int kMaxMatches = 200;
    for (const auto& item : all_items) {
      const std::string filter_text = symbol_insert_name(item.label);
      if (prefix_match(filter_text, query)) {
        matches.push_back(item);
        if (static_cast<int>(matches.size()) >= kMaxMatches) {
          break;
        }
      }
    }
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
  }

  void sync_symbols(WorkspaceModel* workspace,
                    const std::shared_ptr<ISymbolProvider>& symbols,
                    SymbolWorkspaceIndexer* symbol_indexer) {
    const std::string workspace_root = workspace->root;
    const std::string path =
        workspace->buffer.path.empty() ? workspace->active_file : workspace->buffer.path;
    workspace->buffer.ensure_cursors();
    const int line = workspace->buffer.primary_line();
    const int col = workspace->buffer.primary_col();

    index_scanning = symbol_indexer != nullptr && symbol_indexer->scanning();
    workspace_index = false;
    semantic_mode = false;

    const std::string sync_key =
        path + "|sync|" + std::to_string(line) + "|" + std::to_string(col) + "|" +
        std::to_string(workspace->buffer.view_token);
    if (fetch_key == sync_key && !all_items.empty()) {
      merge_local_scope_items(workspace);
      refresh_matches();
      return;
    }

    fetch_key = sync_key;
    all_items.clear();

    if (symbol_indexer != nullptr && !workspace_root.empty()) {
      const auto snap = symbol_indexer->snapshot();
      if (snap && snap->workspace_root == workspace_root) {
        index_scanning = symbol_indexer->scanning();
        if (!snap->symbols.empty()) {
          workspace_index = true;
          for (const auto& sym : snap->symbols) {
            all_items.push_back(from_indexed(sym));
          }
        }
      }
    }

    if (all_items.empty() && symbols && !symbols->indexes_workspace_bulk() &&
        !workspace_root.empty()) {
      workspace_index = true;
      auto syms = symbols->workspace_symbols(workspace_root, query);
      if (syms.empty() && !path.empty()) {
        syms = symbols->symbols_for_file(path);
      }
      for (const auto& sym : syms) {
        all_items.push_back(from_symbol(sym));
      }
    }

    if (all_items.empty() && symbols && !path.empty()) {
      for (const auto& sym : symbols->symbols_for_file(path)) {
        all_items.push_back(from_symbol(sym));
      }
    }

    if (symbols && symbols->supports_semantic_completion() && !path.empty()) {
      CompletionParams params;
      params.path = path;
      params.text = buffer_text(workspace->buffer);
      params.line = line;
      params.character = col;
      auto lsp_items = symbols->completions_at(params);
      if (!lsp_items.empty()) {
        semantic_mode = true;
        std::vector<CompletionItem> merged = std::move(lsp_items);
        merge_completion_items(&merged, all_items);
        all_items = std::move(merged);
      }
    }

    merge_local_scope_items(workspace);
    refresh_matches();
  }

  void close(MainLayoutState* layout_state) {
    open = false;
    live_mode = false;
    prefix.clear();
    query.clear();
    fetch_key.clear();
    all_items.clear();
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
  SourceLocation loc = resolve_symbol_navigation(*symbols, params, declaration);
  if (!loc.valid) {
    workspace->status_message = declaration ? "Sin declaración LSP" : "Sin definición LSP";
    return false;
  }
  flash_symbol_at_cursor(workspace, layout_state, panel_state, line, col, visible_lines);
  schedule_editor_navigation(layout_state, loc);
  return true;
}

void prepare_completion_at_cursor(CompletionState* completion, EditorBuffer* buffer) {
  if (completion == nullptr || buffer == nullptr) {
    return;
  }
  completion->prefix = word_at_cursor(*buffer, buffer->primary());
  completion->query = completion->prefix;
  completion->selected = 0;
  completion->replace_line = buffer->primary().head.line;
  ident_range_at_cursor(*buffer, buffer->primary(), &completion->replace_start,
                        &completion->replace_end);
}

void update_live_completion(CompletionState* completion, WorkspaceModel* workspace,
                            const std::shared_ptr<ISymbolProvider>& symbols,
                            SymbolWorkspaceIndexer* symbol_indexer,
                            MainLayoutState* layout_state, EditorBuffer* buffer) {
  if (completion == nullptr || workspace == nullptr || symbols == nullptr ||
      buffer->path.empty()) {
    return;
  }
  if (!completion_allowed_at_cursor(*buffer)) {
    completion->close(layout_state);
    return;
  }

  prepare_completion_at_cursor(completion, buffer);
  if (completion->prefix.empty()) {
    completion->close(layout_state);
    return;
  }

  completion->open = true;
  completion->live_mode = true;
  completion->sync_symbols(workspace, symbols, symbol_indexer);
  if (completion->matches.empty()) {
    completion->close(layout_state);
  }
}

void maybe_open_live_completion(CompletionState* completion, WorkspaceModel* workspace,
                                const std::shared_ptr<ISymbolProvider>& symbols,
                                SymbolWorkspaceIndexer* symbol_indexer,
                                MainLayoutState* layout_state, EditorBuffer* buffer,
                                char typed) {
  if (!is_ident_char(typed) || symbols == nullptr || buffer->path.empty() ||
      !completion_allowed_at_cursor(*buffer)) {
    if (completion != nullptr && completion->open && completion->live_mode) {
      completion->close(layout_state);
    }
    return;
  }
  if (completion != nullptr && completion->open && !completion->live_mode) {
    return;
  }
  update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state, buffer);
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

Element render_source_flash_line(const std::string& line, int start_col, int end_col,
                                 const Decorator& row_bg) {
  const int len = static_cast<int>(line.size());
  const int start = std::max(0, std::min(start_col, len));
  const int end = std::max(start, std::min(end_col, len));
  const std::string prefix = line.substr(0, static_cast<std::size_t>(start));
  const std::string symbol =
      line.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
  const std::string suffix = line.substr(static_cast<std::size_t>(end));
  Elements parts;
  parts.push_back(text(prefix.empty() ? " " : prefix));
  if (!symbol.empty()) {
    parts.push_back(text(symbol) | bold | inverted | bgcolor(theme::TabPressed()));
  }
  if (!suffix.empty()) {
    parts.push_back(text(suffix));
  }
  return hbox(std::move(parts)) | row_bg;
}

std::optional<Element> try_make_editor_symbol_flash_overlay(MainLayoutState* layout_state,
                                                            const EditorBuffer& buffer,
                                                            const EditorPanelState& panel) {
  if (layout_state == nullptr) {
    return std::nullopt;
  }
  if (!editor_symbol_press_visible(layout_state)) {
    return std::nullopt;
  }
  const auto& press = layout_state->editor_symbol_press;
  if (press.line < 0 || press.line >= static_cast<int>(buffer.lines.size())) {
    return std::nullopt;
  }
  const int row =
      press.render_row >= 0 ? press.render_row : press.line - buffer.scroll;
  if (row < 0) {
    return std::nullopt;
  }
  const std::string& line = buffer.lines[static_cast<std::size_t>(press.line)];
  if (press.start_col < 0 || press.end_col > static_cast<int>(line.size()) ||
      press.end_col <= press.start_col) {
    return std::nullopt;
  }
  const std::string segment = line.substr(static_cast<std::size_t>(press.start_col),
                                          static_cast<std::size_t>(press.end_col - press.start_col));
  const int total = static_cast<int>(buffer.lines.size());
  const int gutter_w = line_number_width(total);
  const int x_pad = gutter_w + 2 + press.start_col;
  const int y_pad = row;

  Element flash = text(segment) | bold | inverted | bgcolor(theme::TabPressed());
  return dbox({text(""),
               vbox({filler() | size(HEIGHT, EQUAL, y_pad),
                     hbox({filler() | size(WIDTH, EQUAL, x_pad), std::move(flash), filler()}),
                     filler()}) |
                     flex});
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
  const int line = std::max(0, std::min(buffer.scroll + row, max_line));

  int col = 0;
  if (in_code) {
    col = std::max(0, m.x - panel.code_box.x_min + buffer.scroll_col);
    const int line_len = static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
    col = std::min(col, line_len);
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
  const CursorPos pos = mouse_to_cursor(m, panel, *buffer, visible_lines);
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
      const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
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
      const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
      apply_mouse_drag_line_select(buffer, panel, pos, visible_lines, panel->code_width_chars);
    } else if (panel->word_select_drag) {
      autoscroll_on_drag(buffer, m, *panel, visible_lines, panel->code_width_chars);
      const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
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
    track_hover_mouse(panel, m, *buffer, visible_lines);
    return false;
  }

  if (m.button == Mouse::WheelLeft || (m.shift && m.button == Mouse::WheelUp)) {
    scroll_view_by_columns(buffer, -3, panel->code_width_chars);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelRight || (m.shift && m.button == Mouse::WheelDown)) {
    scroll_view_by_columns(buffer, 3, panel->code_width_chars);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button == Mouse::WheelUp && !m.shift) {
    scroll_view_by_lines(buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown && !m.shift) {
    scroll_view_by_lines(buffer, 3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button == Mouse::Right && m.motion == Mouse::Pressed && in_code) {
    claim_editor_focus(focus, layout_state, panel->panel_focus);
    const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
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
                                      start_col, end_col, symbol, show_call_hierarchy);
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

    if (in_gutter && debug_model != nullptr && on_command && !buffer->path.empty()) {
      const int row = m.y - panel->gutter_box.y_min;
      if (row >= 0 && row < panel->gutter_visible_rows) {
        const int line = panel->gutter_scroll_start + row + 1;
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
          cached_file_diagnostics(panel, symbols.get(), buffer->path);
      if (handle_gutter_marker_click(panel, diagnostic_modal, file_diag, m)) {
        end_mouse_selection(panel);
        return true;
      }
    }

    const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
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
                     EditorFindState* find, MainLayoutState* layout_state) {
  // #region agent log
  {
    const bool allowed = completion_allowed_at_cursor(*buffer);
    std::ostringstream data;
    data << "{\"allowed\":" << (allowed ? "true" : "false")
         << ",\"has_symbols\":" << (symbols != nullptr ? "true" : "false") << "}";
    editor_debug_agent_log("D", "editor_panel.cpp:open_completion", "enter", data.str());
  }
  // #endregion
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
  completion->live_mode = symbols != nullptr;
  completion->prefix = word_at_cursor(*buffer, buffer->primary());
  completion->query = completion->prefix;
  completion->selected = 0;
  completion->replace_line = buffer->primary().head.line;
  ident_range_at_cursor(*buffer, buffer->primary(), &completion->replace_start,
                        &completion->replace_end);
  completion->fetch_key.clear();
  completion->all_items.clear();
  completion->matches.clear();
  completion->semantic_mode = false;
  completion->sync_symbols(workspace, symbols, symbol_indexer);
  if (layout_state != nullptr && !completion->live_mode) {
    layout_state->text_input_focus = TextInputFocus::EditorCompletion;
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
  const auto& item = completion->matches[static_cast<std::size_t>(completion->selected)];
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

  const bool callable =
      item.kind == SymbolKind::kFunction || item.kind == SymbolKind::kMethod;

  const std::string signature = item.detail.empty() ? item.label : item.detail;
  const bool treat_as_snippet =
      item.insert_format == InsertTextFormat::kSnippet ||
      raw_insert.find('$') != std::string::npos;
  const bool empty_callable_snippet =
      callable && treat_as_snippet && completion_insert_is_empty_call(raw_insert);

  SnippetResult snippet;
  if (callable && (!treat_as_snippet || empty_callable_snippet)) {
    snippet = finalize_function_call_insert(raw_insert, signature, true, paren_already_there);
  } else if (treat_as_snippet) {
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
    completion->sync_symbols(workspace, symbols, symbol_indexer);
  }

  if (event == Event::Escape) {
    completion->close(layout_state);
    return true;
  }
  if (event == Event::Return || event == Event::Tab) {
    return accept_completion(completion, buffer, layout_state, visible_lines, workspace, panel,
                           symbols);
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    if (!completion->matches.empty()) {
      completion->selected = std::min(completion->selected + 1,
                                      static_cast<int>(completion->matches.size()) - 1);
    }
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    completion->selected = std::max(0, completion->selected - 1);
    return true;
  }
  if (event_is_completion(event)) {
    if (!completion->matches.empty()) {
      completion->selected =
          (completion->selected + 1) % static_cast<int>(completion->matches.size());
    }
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
      completion->query += ch;
      completion->selected = 0;
      completion->refresh_matches();
    }
    return true;
  }
  return true;
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

  if (completion != nullptr && completion->open) {
    if (handle_completion_keys(completion, workspace, symbols, symbol_indexer, layout_state, panel,
                               buffer, event, visible_lines)) {
      return true;
    }
    if (completion->live_mode) {
      // Live completion: typing continues in the editor.
    } else {
      return true;
    }
  }

  if (goto_state != nullptr && goto_state->open) {
    return handle_goto_line_keys(goto_state, layout_state, workspace, buffer, event,
                                 visible_lines);
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
  if (event_is_completion(event)) {
    // #region agent log
    {
      const std::string& input = event.input();
      std::ostringstream data;
      data << "{\"input_hex\":\"" << editor_debug_bytes_hex(input)
           << "\",\"is_ctrl_space\":" << (event_is_ctrl_space(event) ? "true" : "false") << "}";
      editor_debug_agent_log("D", "editor_panel.cpp:handle_editor_keys", "completion_shortcut",
                             data.str());
    }
    // #endregion
    open_completion(completion, workspace, symbols, symbol_indexer, buffer, find, layout_state);
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
  if (event == Event::Tab) {
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

  const bool extend = event_is_shift_left(event) || event_is_shift_right(event) ||
                      event_is_shift_up(event) || event_is_shift_down(event) ||
                      event_is_ctrl_alt_left(event) || event_is_ctrl_alt_right(event) ||
                      event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event) ||
                      event_is_shift_home(event) || event_is_shift_end(event);

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
    extend_block_selection_vertical(buffer, -1);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    return true;
  }
  if (event_is_ctrl_alt_down(event) || event_is_ctrl_shift_down(event)) {
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
  if (event_is_ctrl_backspace(event)) {
    delete_word_backward(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event_is_ctrl_delete(event)) {
    delete_word_forward(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event == Event::Backspace) {
    backspace(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event == Event::Delete) {
    delete_char(buffer);
    ensure_scroll_visible(buffer, visible_lines, panel->code_width_chars);
    notify_editor_buffer_changed(workspace, panel, symbols);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
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
                                 buffer, typed);
      return true;
    }
  }
  return false;
}

Element make_completion_overlay(const CompletionState& completion_state,
                                const EditorBuffer& buffer, int gutter_width,
                                int visible_lines) {
  if (!completion_state.open || completion_state.matches.empty()) {
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
    const auto& item = completion_state.matches[static_cast<std::size_t>(i)];
    std::string label =
        completion_state.semantic_mode ? item.label : symbol_insert_name(item.label);
    if (completion_state.workspace_index && !item.file.empty()) {
      label = item.file + " · " + label;
    }
    if (completion_state.semantic_mode && !item.detail.empty()) {
      label += "  " + item.detail;
    }
    Element row = text(" " + label) | color(theme::Header());
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
      text("Ir a línea") | color(theme::Accent()),
      vbox({ModalInputLine(input_line),
            text("Enter ir  Esc cancelar") | color(theme::Muted())}));
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
    if (panel_state != nullptr) {
      panel_state->source_flash.line = line;
      panel_state->source_flash.start_col = start_col;
      panel_state->source_flash.end_col = end_col;
      panel_state->source_flash.until =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    }
    buffer.view_token++;
  }
}

}  // namespace

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
    if (git_history_state->open) {
      return make_git_history_modal(*git_history_state);
    }
    if (diagnostic_state->open) {
      return make_diagnostic_modal(*diagnostic_state);
    }
    if (completion_state->open) {
      workspace->ensure_buffer();
      const EditorBuffer& buffer = workspace->buffer;
      completion_state->sync_symbols(workspace, symbols, symbol_indexer);
      const int total = static_cast<int>(buffer.lines.size());
      const int gutter_w = line_number_width(total);
      const int visible = visible_line_count(panel_state->code_box);
      return make_completion_overlay(*completion_state, buffer, gutter_w, visible);
    }
    if (goto_state->open) {
      return make_goto_line_overlay(*goto_state);
    }
    if (layout_state != nullptr && !completion_state->open && !diagnostic_state->open &&
        !git_history_state->open) {
      workspace->ensure_buffer();
      if (auto flash_overlay = try_make_editor_symbol_flash_overlay(
              layout_state, workspace->buffer, *panel_state)) {
        return *flash_overlay;
      }
      if (editor_symbol_press_visible(layout_state)) {
        return text("");
      }
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
        hbox({text(" Buscar: ") | color(theme::Muted()),
              RenderBlinkInputLine(find_state->query, find_state->cursor_pos, find_focused) | flex,
              text(" (" + std::to_string(find_state->matches.size()) + ") Enter Esc ") |
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
    EditorBuffer& buffer = workspace->buffer;
    buffer.ensure_cursors();

    const bool path_changed = buffer.path != panel_state->last_path;

    if (path_changed) {
      panel_state->last_path = buffer.path;
      panel_state->cached_symbols_path.clear();
      panel_state->cached_semantic_path.clear();
      panel_state->last_semantic_highlight_revision = 0;
      panel_state->semantic_tokens_enqueue_pending = !buffer.path.empty();
      panel_state->last_semantic_highlight_revision_tick = 0;
      buffer.scroll_col = 0;
      clear_hover_state(&panel_state->hover);
      panel_state->document_open_pending = !buffer.path.empty();
      panel_state->pending_document_open_path = buffer.path;
      if (layout_state != nullptr && layout_state->schedule_ui_tick) {
        layout_state->schedule_ui_tick();
      }
      buffer.scroll = std::max(0, buffer.primary_line() - 2);
    }

    const SemanticTokenDocument* semantic_tokens = nullptr;
    if (symbols && symbols->supports_semantic_highlight() && !buffer.path.empty() &&
        is_indexed_source_path(buffer.path)) {
      const uint64_t semantic_rev = symbols->semantic_highlight_revision();
      if (panel_state->cached_semantic_path != buffer.path ||
          semantic_rev != panel_state->last_semantic_highlight_revision) {
        panel_state->cached_semantic_path = buffer.path;
        panel_state->last_semantic_highlight_revision = semantic_rev;
        panel_state->cached_semantic_tokens =
            symbols->semantic_tokens_for_file(buffer.path);
      }
      if (panel_state->cached_semantic_tokens.ready) {
        semantic_tokens = &panel_state->cached_semantic_tokens;
      }
    }

    if (buffer.view_token != panel_state->last_view_token) {
      panel_state->last_view_token = buffer.view_token;
      mark_editor_content_edited(panel_state.get(), buffer);
    }

    const int visible = visible_line_count(panel_state->code_box);

    const int total = static_cast<int>(buffer.lines.size());
    const int start = buffer.scroll;
    const int end = std::min(total, start + visible);
    const int gutter_w = line_number_width(total);
    const bool editor_focused = focus->region == panel_state->panel_focus;
    const std::vector<TextMatch>* find_matches =
        find_state->open && !find_state->matches.empty() ? &find_state->matches : nullptr;
    const std::vector<TextMatch>* selection_occurrences =
        selection_occurrence_matches_for(panel_state.get(), buffer, find_state->open);

    const BracketPairHighlight& bracket =
        cached_bracket_highlight(panel_state.get(), buffer, editor_focused);

    const std::vector<SymbolInfo>& file_symbols =
        cached_file_symbols(panel_state.get(), buffer.path, symbols.get());
    const std::vector<StickyLine> sticky_lines =
        layout_state != nullptr && layout_state->app_settings != nullptr &&
                layout_state->app_settings->sticky_scroll_enabled
            ? sticky_lines_for_scroll(file_symbols, buffer.lines, start, 3)
            : std::vector<StickyLine>{};

    DocumentDiagnostics file_diag;
    if (symbols && symbols->supports_diagnostics() && !buffer.path.empty() &&
        is_lsp_trackable_path(buffer.path)) {
      file_diag = cached_file_diagnostics(panel_state.get(), symbols.get(), buffer.path);
      rebuild_diagnostics_by_line(panel_state.get(), file_diag,
                                  panel_state->cached_file_diag_revision);
    }
    const bool has_git_markers =
        git_service != nullptr && git_service->is_repo() && !panel_state->git_changed_lines.empty();
    const bool has_breakpoint_markers =
        debug_model != nullptr && !buffer.path.empty();
    const bool gutter_markers =
        has_breakpoint_markers || !file_diag.items.empty() || has_git_markers;
    panel_state->gutter_scroll_start = start;
    panel_state->gutter_visible_rows = std::max(0, end - start);

    const int code_width =
        panel_state->code_box.x_max > panel_state->code_box.x_min
            ? panel_state->code_box.x_max - panel_state->code_box.x_min + 1
            : 80;
    panel_state->code_width_chars = code_width;

    track_editor_scroll(panel_state.get(), start, layout_state);
    rebuild_diagnostic_suffix_cache(panel_state.get(), buffer, code_width,
                                    panel_state->cached_file_diag_revision);

    const bool suffixes_enabled =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->show_diagnostic_suffixes;
    const bool show_caret =
        !panel_state->mouse_selecting && !buffer.primary().has_selection();

    Elements gutter_rows;
    Elements code_rows;
    bool in_block_comment = false;
    if (start < static_cast<int>(panel_state->block_comment_line_starts.size())) {
      in_block_comment = panel_state->block_comment_line_starts[static_cast<std::size_t>(start)];
    }
    for (int i = start; i < end; ++i) {
      const bool is_primary = (i == buffer.primary_line());
      const Decorator row_bg =
          is_primary ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());

      if (gutter_markers) {
        char marker = ' ';
        if (has_breakpoint_markers &&
            debug_model->has_breakpoint(normalize_path(buffer.path), i + 1)) {
          marker = '\0';  // rendered as bullet below
        } else {
          marker = line_gutter_marker(panel_state.get(), i);
        }
        std::string gutter_text;
        if (has_breakpoint_markers &&
            debug_model->has_breakpoint(normalize_path(buffer.path), i + 1)) {
          gutter_text = "●";
        } else {
          gutter_text.assign(1, marker == '\0' ? ' ' : marker);
        }
        gutter_text += format_line_number(i + 1, gutter_w);
        Color gutter_color = theme::Muted();
        if (has_breakpoint_markers &&
            debug_model->has_breakpoint(normalize_path(buffer.path), i + 1)) {
          gutter_color = Color::Red;
        } else if (marker == '!') {
          gutter_color = theme::Error();
        } else if (marker == 'G') {
          gutter_color = theme::Success();
        } else if (marker == 'W') {
          gutter_color = theme::Warning();
        }
        gutter_rows.push_back(text(gutter_text) | color(gutter_color) | row_bg);
      } else {
        gutter_rows.push_back(text(format_line_number(i + 1, gutter_w)) | color(theme::Muted()) |
                              row_bg);
      }

      const std::string& display_line = buffer.lines[static_cast<std::size_t>(i)];

      const std::string* suffix_ptr = nullptr;
      const std::vector<Diagnostic>* suffix_color_ptr = nullptr;
      if (show_diagnostic_suffix_on_line(*panel_state, i, buffer, suffixes_enabled)) {
        const auto suffix_it = panel_state->diagnostic_suffix_by_line.find(i);
        if (suffix_it != panel_state->diagnostic_suffix_by_line.end()) {
          suffix_ptr = &suffix_it->second;
          suffix_color_ptr = diagnostics_for_editor_line(panel_state.get(), i);
        }
      }

      const auto& sf = panel_state->source_flash;
      if (sf.active() && sf.line == i) {
        code_rows.push_back(
            render_source_flash_line(display_line, sf.start_col, sf.end_col, row_bg));
        continue;
      }

      EditorSymbolPress symbol_press;
      if (layout_state != nullptr && layout_state->editor_symbol_press.line == i) {
        const auto& press = layout_state->editor_symbol_press;
        if (editor_symbol_press_visible(layout_state)) {
          symbol_press = {press.start_col, press.end_col, true};
        }
      }

      CppHighlightContext highlight_ctx;
      highlight_ctx.in_block_comment = in_block_comment;

      code_rows.push_back(RenderEditorLine(display_line, i, buffer,
                                           editor_focused, find_matches, selection_occurrences,
                                           semantic_tokens,
                                           &bracket, nullptr, suffix_ptr, suffix_color_ptr,
                                           &symbol_press, show_caret, buffer.scroll_col,
                                           code_width, &highlight_ctx) |
                            xflex_shrink);

      in_block_comment = block_comment_state_after_line(display_line, in_block_comment);
    }
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
        vertical_scrollbar(total, buffer.scroll, visible, rendered_lines, scroll_hovered,
                           scroll_active) |
        reflect(panel_state->scrollbar_box);
    panel_state->scrollbar_layout =
        compute_scrollbar_layout(total, buffer.scroll, visible, rendered_lines);

    Elements editor_columns = {gutter, separator() | color(theme::AccentDim()), code | flex};
    if (overview_ruler_enabled) {
      OverviewRulerInput ruler_input;
      ruler_input.total_lines = total;
      ruler_input.scroll = buffer.scroll;
      ruler_input.visible_lines = visible;
      ruler_input.diagnostics_by_line = &panel_state->diagnostics_by_line;
      ruler_input.git_changed_lines = &panel_state->git_changed_lines;
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
                                     panel_state->block_comment_line_starts)});
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
    return handle_editor_keys(workspace, focus, find_state.get(), goto_state.get(),
                              completion_state.get(), panel_state.get(), diagnostic_state.get(),
                              git_history_state.get(), symbols, file_indexer, symbol_indexer,
                              layout_state, debug_model, on_command, event, visible);
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

  auto dispatch_editor_modifiers = [](Event& /*event*/) {};

  if (handlers != nullptr) {
    handlers->key_handler = dispatch_editor_keys;
    handlers->mouse_handler = dispatch_editor_mouse;
    handlers->chrome_mouse_handler = dispatch_editor_chrome_mouse;
    handlers->modifier_handler = dispatch_editor_modifiers;
    handlers->visible_line_count = [panel_state]() {
      return visible_line_count(panel_state->code_box);
    };
    handlers->tick_callback = [workspace, panel_state, find_state, symbols, layout_state,
                               file_indexer, git_service, focus, panel_focus]() {
      if (panel_focus == FocusRegion::SecondaryEditor && workspace->tabs.empty() &&
          focus != nullptr && focus->region == FocusRegion::SecondaryEditor) {
        focus->region = FocusRegion::Editor;
        if (layout_state != nullptr) {
          layout_state->focus_sync_needed = true;
        }
      }
      const int visible = visible_line_count(panel_state->code_box);
      tick_pending_editor_navigation(layout_state, [&](const SourceLocation& loc) {
        navigate_to_location(workspace, layout_state, loc, visible);
        panel_state->source_flash.clear();
      });
      editor_hover_tick(workspace, panel_state.get(), symbols);
      workspace->ensure_buffer();
      tick_selection_occurrence_matches(panel_state.get(), workspace->buffer, layout_state);
      if (find_state->tick_matches(workspace->buffer) && layout_state != nullptr) {
        layout_state->request_ui_tick = true;
      }
      tick_block_comment_cache(panel_state.get(), workspace->buffer);
      tick_lsp_buffer_sync(panel_state.get(), workspace, symbols);
      if (symbols && workspace != nullptr) {
        const std::string& path = workspace->buffer.path;
        if (!path.empty()) {
          if (panel_state->document_open_pending && symbols) {
            panel_state->document_open_pending = false;
            symbols->on_document_opened(panel_state->pending_document_open_path,
                                        buffer_text(workspace->buffer));
          }
          symbols->tick_debounced_updates();
          if (panel_state->symbols_fetch_pending) {
            ensure_file_symbols(panel_state.get(), symbols.get(), path);
          }
          const uint64_t sym_rev = symbols->document_symbols_revision();
          if (sym_rev != panel_state->last_document_symbols_revision) {
            panel_state->last_document_symbols_revision = sym_rev;
            panel_state->symbols_fetch_pending = true;
            ensure_file_symbols(panel_state.get(), symbols.get(), path);
          }
          if (symbols->supports_semantic_highlight() && !path.empty()) {
            const uint64_t sem_rev = symbols->semantic_highlight_revision();
            if (sem_rev != panel_state->last_semantic_highlight_revision_tick) {
              panel_state->last_semantic_highlight_revision_tick = sem_rev;
              if (!symbols->semantic_tokens_for_file(path).ready) {
                panel_state->semantic_tokens_enqueue_pending = true;
              }
            }
            if (panel_state->semantic_tokens_enqueue_pending) {
              symbols->ensure_semantic_tokens(path);
              panel_state->semantic_tokens_enqueue_pending = false;
            }
          }
          sync_diagnostic_cache(panel_state.get(), symbols.get(), workspace, file_indexer);
        }
      }
      if (git_service != nullptr && git_service->is_repo()) {
        workspace->ensure_buffer();
        const EditorBuffer& buf = workspace->buffer;
        const bool settled = editor_content_settled(*panel_state);
        sync_git_cache(panel_state.get(), git_service, buf);
        if (!settled || panel_state->lsp_sync_pending ||
            panel_state->block_comment_token != buf.view_token) {
          layout_state->request_ui_tick = true;
        }
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
