#include "ui/editor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <unordered_map>

#include "editor/bracket_match.hpp"
#include "editor/clipboard.hpp"
#include "editor/editor_context.hpp"
#include "editor/editor_find_state.hpp"
#include "editor/editor_render.hpp"
#include "lsp/diagnostics.hpp"
#include "editor/editor_state.hpp"
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
#include "ui/diagnostics_panel.hpp"
#include "ui/key_bindings.hpp"
#include "ui/panel.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/text_input_style.hpp"
#include "util/path_normalize.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

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

struct EditorPanelState {
  Box code_box;
  Box gutter_box;
  Box breadcrumb_box;
  Box problems_button_box;
  Box scrollbar_box;
  ScrollbarLayout scrollbar_layout;
  bool scrollbar_dragging = false;
  int scrollbar_drag_offset = 0;
  uint64_t last_view_token = 0;
  std::string last_path;
  bool mouse_selecting = false;
  CapturedMouse captured_mouse;
  int last_click_line = -1;
  int last_click_col = -1;
  int64_t last_click_ms = 0;
  bool keyboard_shift = false;
  int64_t last_shift_activity_ms = 0;
  bool extend_click_armed = false;
  CursorPos shift_extend_anchor;
  bool shift_extend_anchor_valid = false;
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
  int problem_errors = 0;
  int problem_warnings = 0;
  DocumentDiagnostics cached_file_diag;
  uint64_t cached_file_diag_revision = 0;
  int gutter_scroll_start = 0;
  int gutter_visible_rows = 0;
  bool symbols_fetch_pending = false;
  uint64_t last_document_symbols_revision = 0;
  SemanticTokenDocument cached_semantic_tokens;
  std::string cached_semantic_path;
  uint64_t last_semantic_highlight_revision = 0;
  std::unordered_map<int, std::vector<Diagnostic>> diagnostics_by_line;
  uint64_t diagnostics_by_line_revision = 0;
  std::string diagnostics_by_line_path;
  std::unordered_map<int, std::string> diagnostic_suffix_by_line;
  int diagnostic_suffix_code_width = 0;
  uint64_t diagnostic_suffix_view_token = 0;
  uint64_t diagnostic_suffix_revision = 0;
  int last_render_scroll = 0;
  int64_t last_scroll_change_ms = 0;
  bool document_open_pending = false;
  std::string pending_document_open_path;
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
};

void flash_symbol_at_buffer_pos_impl(WorkspaceModel* workspace, MainLayoutState* layout_state,
                                     EditorPanelState* panel_state, int line, int col,
                                     int visible_lines);

constexpr int kDoubleClickMs = 400;
constexpr int kShiftClickWindowMs = 3000;
constexpr int kHoverDelayMs = 500;
constexpr int kSuffixScrollSettleMs = 150;

CursorPos mouse_to_cursor(const Mouse& m, const EditorPanelState& panel, const EditorBuffer& buffer,
                          int visible_lines);
void end_mouse_selection(EditorPanelState* panel);

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

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

void claim_editor_focus(FocusManagerState* focus, MainLayoutState* layout_state) {
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
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
  if (!symbols->symbols_lsp_pending(path)) {
    panel->symbols_fetch_pending = false;
  }
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

void sync_diagnostic_cache(EditorPanelState* panel, ISymbolProvider* symbols,
                           const std::string& path) {
  if (panel == nullptr || symbols == nullptr || !symbols->supports_diagnostics()) {
    return;
  }
  const uint64_t revision = symbols->diagnostics_revision();
  if (revision == panel->last_diag_revision) {
    return;
  }
  panel->last_diag_revision = revision;
  const auto docs = symbols->workspace_diagnostics();
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
    }
    return;
  }

  HoverParams params;
  params.path = buffer.path;
  params.text = buffer_text(buffer);
  params.line = hover.line;
  params.character = hover.col;
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

bool handle_problems_button_click(MainLayoutState* layout_state, EditorPanelState* panel,
                                  const Mouse& m) {
  if (layout_state == nullptr || panel == nullptr || m.button != Mouse::Left ||
      m.motion != Mouse::Pressed) {
    return false;
  }
  if (panel->problems_button_box.IsEmpty() || !panel->problems_button_box.Contain(m.x, m.y)) {
    return false;
  }
  trigger_press(layout_state, press_id::kEditorProblems);
  layout_state->diagnostics_panel_visible = !layout_state->diagnostics_panel_visible;
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
      claim_editor_focus(focus, layout_state);
      ensure_scroll_visible(&workspace->buffer, visible_lines);
      return true;
    }
  }
  return false;
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
    claim_editor_focus(focus, layout_state);
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
  if (handle_tab_bar_mouse(workspace, focus, tab_bar, m, layout_state)) {
    claim_editor_focus(focus, layout_state);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
    return true;
  }
  if (handle_problems_button_click(layout_state, panel, m)) {
    return true;
  }
  if (handle_breadcrumb_click(workspace, focus, layout_state, panel, m, visible_lines)) {
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
  const char marker = line_diagnostic_marker(line, file_diag);
  if (marker == '\0') {
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
  if (layout_state != nullptr && layout_state->diagnostics_panel_visible) {
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
  } else if (layout_state != nullptr && layout_state->diagnostics_panel_visible) {
    problems_btn = problems_btn | bgcolor(theme::TabActive());
  }
  problems_btn = problems_btn | reflect(panel_state->problems_button_box);

  Element bar = hbox({
                  text(" "),
                  hbox(std::move(segments)),
                  text(meta) | color(theme::Muted()),
                  problems_btn,
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

Element make_sticky_overlay(const std::vector<StickyLine>& sticky_lines, int gutter_width) {
  if (sticky_lines.empty()) {
    return text("");
  }
  Elements rows;
  for (const StickyLine& sticky : sticky_lines) {
    const std::string indent(static_cast<std::size_t>(sticky.depth * 2), ' ');
    rows.push_back(text(indent + sticky.text) | color(theme::Muted()) | bgcolor(theme::TabIdle()));
  }
  const int sticky_h = static_cast<int>(rows.size());
  return dbox({text(""),
               vbox({hbox({filler() | size(WIDTH, EQUAL, gutter_width + 1),
                           vbox(std::move(rows)) | bgcolor(theme::TabIdle()) | clear_under,
                           filler()}),
                     filler()}) |
                   flex | size(HEIGHT, EQUAL, sticky_h)});
}

bool is_double_click(const EditorPanelState& panel, int line, int col, int64_t now_ms) {
  if (panel.last_click_line != line) {
    return false;
  }
  if (std::abs(panel.last_click_col - col) > 1) {
    return false;
  }
  return now_ms - panel.last_click_ms <= kDoubleClickMs;
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

void arm_extend_click(EditorPanelState* panel) {
  if (panel == nullptr) {
    return;
  }
  panel->extend_click_armed = true;
  panel->keyboard_shift = true;
  panel->last_shift_activity_ms = steady_now_ms();
}

void disarm_extend_click(EditorPanelState* panel) {
  if (panel == nullptr) {
    return;
  }
  panel->extend_click_armed = false;
  panel->keyboard_shift = false;
  panel->last_shift_activity_ms = 0;
}

void update_editor_modifiers(EditorPanelState* panel, Event& event) {
  if (panel == nullptr) {
    return;
  }
  if (event.is_mouse()) {
    const auto& m = event.mouse();
    if (m.shift || m.meta || sgr_mouse_has_shift(event)) {
      panel->keyboard_shift = true;
      panel->last_shift_activity_ms = steady_now_ms();
    }
    return;
  }

  if (event_input_has_shift_release(event) || event_is_shift_key_release(event)) {
    panel->keyboard_shift = false;
  } else if (event_input_has_shift_modifier(event) || event_is_shift_key_press(event)) {
    arm_extend_click(panel);
  }
}

bool shift_extend_click(const EditorPanelState& panel, const Mouse& m, const Event& event) {
  if (m.control && !m.shift && !sgr_mouse_has_shift(event)) {
    return false;
  }
  if (m.shift || m.meta || panel.keyboard_shift || sgr_mouse_has_shift(event) ||
      sgr_mouse_has_meta(event)) {
    return true;
  }
  if (panel.last_shift_activity_ms > 0 &&
      steady_now_ms() - panel.last_shift_activity_ms <= kShiftClickWindowMs) {
    return true;
  }
  return false;
}

void save_shift_extend_anchor(EditorPanelState* panel, const EditorBuffer& buffer) {
  if (panel == nullptr) {
    return;
  }
  panel->shift_extend_anchor = buffer.primary().head;
  panel->shift_extend_anchor_valid = true;
}

void finish_editor_move(EditorPanelState* panel, EditorBuffer* buffer, bool extend_selection) {
  if (extend_selection) {
    arm_extend_click(panel);
    if (buffer != nullptr && buffer->primary().has_selection()) {
      panel->shift_extend_anchor = buffer->primary().anchor;
      panel->shift_extend_anchor_valid = true;
    }
  } else {
    save_shift_extend_anchor(panel, *buffer);
    disarm_extend_click(panel);
  }
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
  SourceLocation loc = declaration ? symbols->goto_declaration(params)
                                   : symbols->goto_definition(params);
  if (!loc.valid && !declaration) {
    loc = symbols->goto_declaration(params);
  }
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

  prepare_completion_at_cursor(completion, buffer);
  if (completion->prefix.empty()) {
    completion->close(layout_state);
    return;
  }

  completion->open = true;
  completion->live_mode = true;
  completion->sync_symbols(workspace, symbols, symbol_indexer);
}

void maybe_open_live_completion(CompletionState* completion, WorkspaceModel* workspace,
                                const std::shared_ptr<ISymbolProvider>& symbols,
                                SymbolWorkspaceIndexer* symbol_indexer,
                                MainLayoutState* layout_state, EditorBuffer* buffer,
                                char typed) {
  if (!is_ident_char(typed) || symbols == nullptr || buffer->path.empty()) {
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
  if (layout_state != nullptr && layout_state->editor_visible_line_count) {
    return std::max(layout_state->editor_visible_line_count(), 1);
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
                   FocusManagerState* focus) {
  if (!find->open) {
    open_find_bar(find, buffer);
  } else {
    find->refresh_matches(*buffer);
    find->cursor_pos = static_cast<int>(find->query.size());
  }
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  if (layout_state != nullptr) {
    layout_state->text_input_focus = TextInputFocus::EditorFind;
    layout_state->focus_sync_needed = true;
  }
}

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
    find->refresh_matches(*buffer);
    find->jump_to_next_match(buffer, visible_lines);
    return true;
  }
  if (event == Event::Backspace) {
    if (find->cursor_pos > 0 && find->cursor_pos <= static_cast<int>(find->query.size())) {
      find->query.erase(static_cast<std::size_t>(find->cursor_pos - 1), 1);
      --find->cursor_pos;
      find->refresh_matches(*buffer);
    }
    return true;
  }
  if (event == Event::Delete) {
    if (find->cursor_pos >= 0 && find->cursor_pos < static_cast<int>(find->query.size())) {
      find->query.erase(static_cast<std::size_t>(find->cursor_pos), 1);
      find->refresh_matches(*buffer);
    }
    return true;
  }
  if (event == Event::ArrowLeft) {
    find->cursor_pos = std::max(0, find->cursor_pos - 1);
    return true;
  }
  if (event == Event::ArrowRight) {
    find->cursor_pos =
        std::min(static_cast<int>(find->query.size()), find->cursor_pos + 1);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (!ch.empty() && ch[0] >= 32 && ch[0] != 127) {
      find->query.insert(static_cast<std::size_t>(find->cursor_pos), ch);
      find->cursor_pos += static_cast<int>(ch.size());
      find->refresh_matches(*buffer);
    }
    return true;
  }
  return true;
}

bool handle_editor_escape(EditorBuffer* buffer, EditorFindState* find,
                          MainLayoutState* layout_state, bool* goto_open,
                          CompletionState* completion, EditorPanelState* panel,
                          DiagnosticModalState* diagnostic_modal) {
  if (panel != nullptr) {
    disarm_extend_click(panel);
    end_mouse_selection(panel);
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
    col = std::max(0, m.x - panel.code_box.x_min);
    const int line_len = static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
    col = std::min(col, line_len);
  } else if (m.x >= panel.code_box.x_min) {
    col = static_cast<int>(buffer.lines[static_cast<std::size_t>(line)].size());
  }

  return {line, col};
}

void autoscroll_on_drag(EditorBuffer* buffer, const Mouse& m, const EditorPanelState& panel,
                        int visible_lines) {
  if (m.y < panel.code_box.y_min && buffer->scroll > 0) {
    scroll_view_by_lines(buffer, -1, visible_lines);
  } else if (m.y > panel.code_box.y_max) {
    scroll_view_by_lines(buffer, 1, visible_lines);
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
  panel->captured_mouse.reset();
}

void apply_mouse_drag_head(EditorBuffer* buffer, const Mouse& m, const EditorPanelState& panel,
                           int visible_lines) {
  autoscroll_on_drag(buffer, m, panel, visible_lines);
  const CursorPos pos = mouse_to_cursor(m, panel, *buffer, visible_lines);
  buffer->primary().head = pos;
  clamp_all_cursors(buffer);
  ensure_scroll_visible(buffer, visible_lines);
}

bool handle_editor_mouse(WorkspaceModel* workspace, FocusManagerState* focus,
                         EditorFindState* find, MainLayoutState* layout_state,
                         EditorPanelState* panel, DiagnosticModalState* diagnostic_modal,
                         const std::shared_ptr<ISymbolProvider>& symbols, Event event,
                         int visible_lines) {
  if (!event.is_mouse()) {
    return false;
  }

  update_editor_modifiers(panel, event);

  EditorBuffer* buffer = &workspace->buffer;
  buffer->ensure_cursors();

  const auto& m = event.mouse();
  const bool in_code = panel->code_box.Contain(m.x, m.y);
  const bool in_gutter = panel->gutter_box.Contain(m.x, m.y);
  const bool in_editor = in_code || in_gutter;

  if (m.button == Mouse::Left && m.motion == Mouse::Moved && panel->mouse_selecting) {
    ensure_mouse_capture(panel, event);
    apply_mouse_drag_head(buffer, m, *panel, visible_lines);
    return true;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Released && panel->mouse_selecting) {
    apply_mouse_drag_head(buffer, m, *panel, visible_lines);
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

  if (m.button == Mouse::WheelUp) {
    scroll_view_by_lines(buffer, -3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }
  if (m.button == Mouse::WheelDown) {
    scroll_view_by_lines(buffer, 3, visible_lines);
    clear_hover_state(&panel->hover);
    return true;
  }

  if (m.button == Mouse::Right && m.motion == Mouse::Pressed && in_code) {
    claim_editor_focus(focus, layout_state);
    const CursorPos pos = mouse_to_cursor(m, *panel, *buffer, visible_lines);
    buffer->reset_to_single_cursor(pos.line, pos.col);
    MultiCursor cursor = buffer->primary();
    cursor.head = pos;
    int start_col = 0;
    int end_col = 0;
    ident_range_at_cursor(*buffer, cursor, &start_col, &end_col);
    const std::string symbol = word_at_cursor(*buffer, cursor);
    if (!symbol.empty() && layout_state != nullptr) {
      context_menu_open_editor_symbol(&layout_state->context_menu, m.x, m.y, pos.line, pos.col,
                                      start_col, end_col, symbol);
      end_mouse_selection(panel);
      return true;
    }
    return false;
  }

  if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
    claim_editor_focus(focus, layout_state);
    if (layout_state != nullptr && find != nullptr && find->open) {
      close_find_bar(find);
      if (layout_state != nullptr) {
        layout_state->text_input_focus = TextInputFocus::None;
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
    const bool shift_click = shift_extend_click(*panel, m, event);

    const int64_t now_ms = steady_now_ms();
    const bool double_click =
        in_code && !shift_click && !m.control && is_double_click(*panel, pos.line, pos.col, now_ms);
    panel->last_click_line = pos.line;
    panel->last_click_col = pos.col;
    panel->last_click_ms = now_ms;

    if (double_click) {
      select_word_at(buffer, pos.line, pos.col);
      save_shift_extend_anchor(panel, *buffer);
      ensure_scroll_visible(buffer, visible_lines);
      return true;
    }

    if (m.control && symbols != nullptr && symbols->supports_navigation() &&
        !buffer->path.empty()) {
      disarm_extend_click(panel);
      if (try_go_to_symbol(workspace, layout_state, panel, symbols, pos.line, pos.col, false,
                         visible_lines)) {
        return true;
      }
    }

    if (shift_click && m.control && symbols != nullptr &&
        symbols->supports_navigation() && !buffer->path.empty()) {
      if (try_go_to_symbol(workspace, layout_state, panel, symbols, pos.line, pos.col, true,
                         visible_lines)) {
        return true;
      }
    }

    if (shift_click) {
      end_mouse_selection(panel);
      if (buffer->multi_cursor_active()) {
        exit_multi_cursor_mode(buffer);
      }
      const CursorPos anchor =
          buffer->primary().has_selection()
              ? buffer->primary().anchor
              : (panel->shift_extend_anchor_valid ? panel->shift_extend_anchor
                                                  : buffer->primary().head);
      buffer->reset_to_single_cursor(anchor.line, anchor.col);
      buffer->primary().anchor = anchor;
      buffer->primary().head = pos;
      clamp_all_cursors(buffer);
      ensure_scroll_visible(buffer, visible_lines);
      buffer->dirty = true;
      buffer->view_token++;
      disarm_extend_click(panel);
      return true;
    }

    if (pos.line != buffer->primary().head.line || pos.col != buffer->primary().head.col) {
      workspace->record_cursor_jump();
    }
    buffer->reset_to_single_cursor(pos.line, pos.col);
    save_shift_extend_anchor(panel, *buffer);
    disarm_extend_click(panel);
    begin_mouse_selection(panel, event);
    ensure_scroll_visible(buffer, visible_lines);
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
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && std::isdigit(static_cast<unsigned char>(ch[0]))) {
      goto_state->query += ch;
    }
    return true;
  }
  return true;
}

void open_completion(CompletionState* completion, WorkspaceModel* workspace,
                     const std::shared_ptr<ISymbolProvider>& symbols,
                     SymbolWorkspaceIndexer* symbol_indexer, EditorBuffer* buffer,
                     EditorFindState* find, MainLayoutState* layout_state) {
  if (completion == nullptr) {
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
                       WorkspaceModel* workspace,
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

  const bool callable =
      item.kind == SymbolKind::kFunction || item.kind == SymbolKind::kMethod;

  SnippetResult snippet;
  if (item.insert_format == InsertTextFormat::kSnippet) {
    snippet = expand_snippet(raw_insert);
  } else if (callable) {
    snippet = finalize_function_call_insert(
        raw_insert, item.detail.empty() ? item.label : item.detail, true);
  } else {
    snippet.text = raw_insert;
    snippet.caret_col = static_cast<int>(raw_insert.size());
  }

  replace_text_range_with_caret(buffer, repl_line, repl_start, repl_end, snippet.text,
                                snippet.caret_line_offset, snippet.caret_col,
                                snippet.sel_start_col, snippet.sel_end_col);
  ensure_scroll_visible(buffer, visible_lines);
  if (symbols && workspace != nullptr && !workspace->buffer.path.empty()) {
    symbols->on_document_changed(workspace->buffer.path, buffer_text(*buffer));
  }
  completion->close(layout_state);
  return true;
}

bool handle_completion_keys(CompletionState* completion, WorkspaceModel* workspace,
                              const std::shared_ptr<ISymbolProvider>& symbols,
                              SymbolWorkspaceIndexer* symbol_indexer,
                              MainLayoutState* layout_state, EditorBuffer* buffer,
                              Event event, int visible_lines) {
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
    return accept_completion(completion, buffer, layout_state, visible_lines, workspace, symbols);
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
                        const std::shared_ptr<ISymbolProvider>& symbols,
                        WorkspaceIndexer* file_indexer,
                        SymbolWorkspaceIndexer* symbol_indexer,
                        MainLayoutState* layout_state, Event event, int visible_lines) {
  if (focus->region != FocusRegion::Editor) {
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
      save_shift_extend_anchor(panel, *buffer);
      return true;
    }
    return false;
  }
  if (event_is_alt_right(event)) {
    if (workspace->navigate_cursor_forward(visible_lines)) {
      save_shift_extend_anchor(panel, *buffer);
      return true;
    }
    return false;
  }

  if (completion != nullptr && completion->open) {
    if (handle_completion_keys(completion, workspace, symbols, symbol_indexer, layout_state,
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
    return handle_editor_escape(buffer, find, layout_state,
                                goto_state != nullptr ? &goto_state->open : nullptr, completion,
                                panel, diagnostic_modal);
  }
  if (event_is_completion(event)) {
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
  if (event_is_ctrl_z(event)) {
    undo_edit(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_c(event)) {
    copy_selection(buffer);
    return true;
  }
  if (event_is_ctrl_v(event)) {
    paste_text(buffer, editor_clipboard());
    ensure_scroll_visible(buffer, visible_lines);
    save_shift_extend_anchor(panel, *buffer);
    return true;
  }
  if (event_is_ctrl_u(event)) {
    move_primary_half_page_up(buffer, visible_lines);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::Tab) {
    insert_tab_stop(buffer, 4);
    ensure_scroll_visible(buffer, visible_lines);
    buffer->view_token++;
    return true;
  }
  if (event_is_ctrl_i(event)) {
    move_primary_half_page_down(buffer, visible_lines);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_d(event) || event_is_ctrl_shift_d(event)) {
    add_next_selection_match(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_shift_l(event)) {
    select_all_matches(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::CtrlS) {
    workspace->save_buffer();
    if (!workspace->root.empty() && !workspace->buffer.path.empty()) {
      std::error_code ec;
      const auto rel = std::filesystem::relative(
          std::filesystem::path(workspace->buffer.path),
          std::filesystem::path(workspace->root), ec);
      if (!ec) {
        const std::string rel_str = rel.generic_string();
        if (symbols) {
          symbols->on_document_changed(workspace->buffer.path, buffer_text(*buffer));
        }
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
                      event_is_ctrl_shift_left(event) || event_is_ctrl_shift_right(event);

  if (event_is_ctrl_left(event) || event_is_ctrl_shift_left(event)) {
    move_primary_word_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event_is_ctrl_right(event) || event_is_ctrl_shift_right(event)) {
    move_primary_word_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event == Event::ArrowLeft || event_is_shift_left(event)) {
    move_primary_left(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event == Event::ArrowRight || event_is_shift_right(event)) {
    move_primary_right(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event_is_ctrl_shift_up(event)) {
    extend_block_selection_vertical(buffer, -1);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event_is_ctrl_shift_down(event)) {
    extend_block_selection_vertical(buffer, 1);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::ArrowUp || event_is_shift_up(event)) {
    move_primary_up(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event == Event::ArrowDown || event_is_shift_down(event)) {
    move_primary_down(buffer, extend);
    ensure_scroll_visible(buffer, visible_lines);
    finish_editor_move(panel, buffer, extend);
    return true;
  }
  if (event == Event::Home) {
    move_primary_home(buffer, false);
    finish_editor_move(panel, buffer, false);
    return true;
  }
  if (event == Event::End) {
    move_primary_end(buffer, false);
    finish_editor_move(panel, buffer, false);
    return true;
  }
  if (event_is_ctrl_backspace(event)) {
    delete_word_backward(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event_is_ctrl_delete(event)) {
    delete_word_forward(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event == Event::Backspace) {
    backspace(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    if (symbols && !buffer->path.empty()) {
      symbols->on_document_changed(buffer->path, buffer_text(*buffer));
    }
    buffer->view_token++;
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event == Event::Delete) {
    delete_char(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    if (symbols && !buffer->path.empty()) {
      symbols->on_document_changed(buffer->path, buffer_text(*buffer));
    }
    buffer->view_token++;
    if (completion != nullptr && completion->open && completion->live_mode) {
      update_live_completion(completion, workspace, symbols, symbol_indexer, layout_state,
                             buffer);
    }
    return true;
  }
  if (event == Event::Return) {
    newline(buffer);
    ensure_scroll_visible(buffer, visible_lines);
    save_shift_extend_anchor(panel, *buffer);
    return true;
  }
  if (event == Event::PageDown) {
    move_primary_page_down(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event == Event::PageUp) {
    move_primary_page_up(buffer, visible_lines, false);
    ensure_scroll_visible(buffer, visible_lines);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      const char typed = ch[0];
      insert_char(buffer, typed);
      ensure_scroll_visible(buffer, visible_lines);
      save_shift_extend_anchor(panel, *buffer);
      if (symbols && !buffer->path.empty()) {
        symbols->on_document_changed(buffer->path, buffer_text(*buffer));
      }
      buffer->view_token++;
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
  if (!completion_state.open) {
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
  if (rows.empty()) {
    const char* msg = completion_state.index_scanning && completion_state.workspace_index
                          ? " indexando…"
                          : completion_state.live_mode ? " …" : " —";
    rows.push_back(text(msg) | color(theme::Muted()) | bgcolor(theme::CodeBg()));
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
                          SymbolWorkspaceIndexer* symbol_indexer) {
  auto panel_state = std::make_shared<EditorPanelState>();
  auto tab_bar_state = std::make_shared<EditorTabBarState>();
  auto find_state = std::make_shared<EditorFindState>();
  auto goto_state = std::make_shared<GotoLineState>();
  auto completion_state = std::make_shared<CompletionState>();
  auto diagnostic_state = std::make_shared<DiagnosticModalState>();

  auto modal_overlay = Renderer([find_state, goto_state, completion_state,
                                 diagnostic_state, workspace, symbols, symbol_indexer,
                                 panel_state, tab_bar_state, layout_state] {
    if (tab_bar_state->overflow_open) {
      return make_tabs_overflow_modal(workspace, tab_bar_state.get());
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
    if (layout_state != nullptr && !completion_state->open && !diagnostic_state->open) {
      workspace->ensure_buffer();
      if (auto flash_overlay = try_make_editor_symbol_flash_overlay(
              layout_state, workspace->buffer, *panel_state)) {
        return *flash_overlay;
      }
      if (editor_symbol_press_visible(layout_state)) {
        return text("");
      }
    }
    if (!find_state->open && !completion_state->open && !diagnostic_state->open) {
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

  auto code_view = Renderer([workspace, focus, panel_state, find_state, symbols, layout_state] {
    workspace->ensure_buffer();
    EditorBuffer& buffer = workspace->buffer;
    buffer.ensure_cursors();

    if (buffer.path != panel_state->last_path) {
      panel_state->last_path = buffer.path;
      panel_state->cached_symbols_path.clear();
      panel_state->cached_semantic_path.clear();
      panel_state->last_semantic_highlight_revision = 0;
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
      if (find_state->open) {
        find_state->refresh_matches(buffer);
      }
    }

    if (find_state->open && !find_state->query.empty()) {
      find_state->refresh_matches(buffer);
    }

    const int visible = visible_line_count(panel_state->code_box);

    const int total = static_cast<int>(buffer.lines.size());
    const int start = buffer.scroll;
    const int end = std::min(total, start + visible);
    const int gutter_w = line_number_width(total);
    const bool editor_focused = focus->region == FocusRegion::Editor;
    const std::vector<TextMatch>* find_matches =
        find_state->open && !find_state->matches.empty() ? &find_state->matches : nullptr;

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
    const bool gutter_markers = !file_diag.items.empty();
    panel_state->gutter_scroll_start = start;
    panel_state->gutter_visible_rows = std::max(0, end - start);

    const int code_width =
        panel_state->code_box.x_max > panel_state->code_box.x_min
            ? panel_state->code_box.x_max - panel_state->code_box.x_min + 1
            : 80;

    track_editor_scroll(panel_state.get(), start, layout_state);
    rebuild_diagnostic_suffix_cache(panel_state.get(), buffer, code_width,
                                    panel_state->cached_file_diag_revision);

    const bool suffixes_enabled =
        layout_state == nullptr || layout_state->app_settings == nullptr ||
        layout_state->app_settings->show_diagnostic_suffixes;
    if (!scroll_suffixes_settled(*panel_state) && layout_state != nullptr &&
        layout_state->schedule_ui_tick) {
      layout_state->schedule_ui_tick();
    }

    const bool show_caret =
        !panel_state->mouse_selecting && !buffer.primary().has_selection();

    Elements gutter_rows;
    Elements code_rows;
    for (int i = start; i < end; ++i) {
      const bool is_primary = (i == buffer.primary_line());
      const Decorator row_bg =
          is_primary ? bgcolor(theme::EditorLineHi()) : bgcolor(theme::CodeBg());

      if (gutter_markers) {
        const char marker = line_diagnostic_marker_from_map(panel_state.get(), i);
        std::string gutter_text(1, marker == '\0' ? ' ' : marker);
        gutter_text += format_line_number(i + 1, gutter_w);
        Color gutter_color = theme::Muted();
        if (marker == '!') {
          gutter_color = theme::Error();
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

      code_rows.push_back(RenderEditorLine(display_line, i, buffer,
                                           editor_focused, find_matches, semantic_tokens,
                                           &bracket, nullptr, suffix_ptr, suffix_color_ptr,
                                           &symbol_press, show_caret));
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
    Element scrollbar =
        vertical_scrollbar(total, buffer.scroll, visible, rendered_lines, scroll_hovered,
                           scroll_active) |
        reflect(panel_state->scrollbar_box);
    panel_state->scrollbar_layout =
        compute_scrollbar_layout(total, buffer.scroll, visible, rendered_lines);
    Element editor_body =
        hbox({gutter, separator() | color(theme::AccentDim()), code | flex, scrollbar});

    Element body = std::move(editor_body) | frame | flex | bgcolor(theme::CodeBg());
    if (sticky_lines.empty()) {
      return body;
    }
    return dbox({std::move(body), make_sticky_overlay(sticky_lines, gutter_w)});
  });

  // En Stacked, el primer hijo se dibuja encima (FTXUI invierte el dbox interno).
  auto editor_stack = Container::Stacked({
      modal_overlay,
      code_view | flex,
  });

  auto diagnostics_panel =
      MakeDiagnosticsPanel(workspace, focus, symbols, layout_state);

  // Sin Container::Vertical+Maybe: con muchas líneas el layout DOM de FTXUI se bloqueaba.
  auto panel = Renderer(editor_stack, [workspace, find_state, editor_stack, panel_state,
                                       tab_bar_state, symbols, layout_state,
                                       diagnostics_panel] {
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
    if (layout_state != nullptr && layout_state->diagnostics_panel_visible) {
      return vbox({std::move(tab_bar), std::move(title), editor | yflex,
                     diagnostics_panel->Render() | size(HEIGHT, EQUAL,
                                                        layout_state->diagnostics_panel_height)}) |
             flex | bgcolor(theme::CodeBg());
    }
    // Misma forma que MakePanel (título + cuerpo) pero con fila de tabs encima; sin flex/filler
    // anidados extra en tabs/breadcrumb que cuelgan FTXUI al abrir archivos.
    return vbox({std::move(tab_bar), std::move(title), PanelBody(std::move(editor), theme::CodeBg())}) |
           flex | bgcolor(theme::CodeBg());
  });

  auto dispatch_editor_keys = [workspace, focus, panel_state, tab_bar_state, find_state,
                               goto_state, completion_state, diagnostic_state, symbols,
                               file_indexer, symbol_indexer, layout_state](Event event) {
    if (tab_bar_state->overflow_open &&
        handle_tabs_overflow_keys(workspace, focus, tab_bar_state.get(), event)) {
      return true;
    }
    if (layout_state != nullptr &&
        layout_state->text_input_focus == TextInputFocus::Console) {
      return false;
    }
    if (focus->region != FocusRegion::Editor) {
      return false;
    }
    workspace->ensure_buffer();
    EditorBuffer* buffer = &workspace->buffer;
    const int visible = visible_line_count(panel_state->code_box);

    if (find_input_active(layout_state, *find_state) &&
        handle_find_keys(find_state.get(), layout_state, buffer, event, visible)) {
      return true;
    }
    if (event_is_ctrl_f(event)) {
      activate_find(find_state.get(), buffer, layout_state, focus);
      return true;
    }
    return handle_editor_keys(workspace, focus, find_state.get(), goto_state.get(),
                              completion_state.get(), panel_state.get(), diagnostic_state.get(),
                              symbols, file_indexer, symbol_indexer, layout_state, event, visible);
  };

  auto dispatch_editor_chrome_mouse = [workspace, focus, panel_state, tab_bar_state,
                                       layout_state](Event event) {
    workspace->ensure_buffer();
    const int visible = visible_line_count(panel_state->code_box);
    return handle_editor_chrome_mouse(workspace, focus, panel_state.get(), tab_bar_state.get(),
                                      layout_state, event, visible);
  };

  auto dispatch_editor_mouse = [workspace, focus, panel_state, find_state, diagnostic_state,
                                  layout_state, symbols, dispatch_editor_chrome_mouse](Event event) {
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
      update_editor_modifiers(panel_state.get(), event);
      return handle_editor_mouse(workspace, focus, find_state.get(), layout_state,
                                 panel_state.get(), diagnostic_state.get(), symbols, event,
                                 visible);
    }

    if (layout_state != nullptr &&
        layout_state->text_input_focus == TextInputFocus::Console) {
      return false;
    }
    if (focus->region != FocusRegion::Editor) {
      return false;
    }
    workspace->ensure_buffer();
    const int visible = visible_line_count(panel_state->code_box);
    update_editor_modifiers(panel_state.get(), event);
    return handle_editor_mouse(workspace, focus, find_state.get(), layout_state,
                               panel_state.get(), diagnostic_state.get(), symbols, event,
                               visible);
  };

  auto dispatch_editor_modifiers = [panel_state](Event event) {
    update_editor_modifiers(panel_state.get(), event);
  };

  if (layout_state != nullptr) {
    layout_state->editor_key_handler = dispatch_editor_keys;
    layout_state->editor_mouse_handler = dispatch_editor_mouse;
    layout_state->editor_chrome_mouse_handler = dispatch_editor_chrome_mouse;
    layout_state->editor_modifier_handler = dispatch_editor_modifiers;
    layout_state->editor_visible_line_count = [panel_state]() {
      return visible_line_count(panel_state->code_box);
    };
    layout_state->editor_tick_callback = [workspace, panel_state, symbols, layout_state]() {
      const int visible = visible_line_count(panel_state->code_box);
      tick_pending_editor_navigation(layout_state, [&](const SourceLocation& loc) {
        navigate_to_location(workspace, layout_state, loc, visible);
        panel_state->source_flash.clear();
      });
      editor_hover_tick(workspace, panel_state.get(), symbols);
      if (symbols && workspace != nullptr) {
        workspace->ensure_buffer();
        const std::string& path = workspace->buffer.path;
        if (!path.empty()) {
          if (panel_state->document_open_pending && symbols) {
            panel_state->document_open_pending = false;
            symbols->on_document_opened(panel_state->pending_document_open_path,
                                        buffer_text(workspace->buffer));
          }
          if (panel_state->symbols_fetch_pending) {
            ensure_file_symbols(panel_state.get(), symbols.get(), path);
          }
          const uint64_t sym_rev = symbols->document_symbols_revision();
          if (sym_rev != panel_state->last_document_symbols_revision) {
            panel_state->last_document_symbols_revision = sym_rev;
            panel_state->symbols_fetch_pending = true;
            ensure_file_symbols(panel_state.get(), symbols.get(), path);
          }
          if (symbols->supports_semantic_highlight()) {
            symbols->ensure_semantic_tokens(path);
          }
          sync_diagnostic_cache(panel_state.get(), symbols.get(), path);
        }
      }
    };
  }

  return WrapFocusable(CatchEvent(panel, [dispatch_editor_keys, dispatch_editor_mouse, workspace,
                                          focus, panel_state, tab_bar_state, find_state,
                                          layout_state, symbols, diagnostics_panel](Event event) {
    if (layout_state != nullptr && layout_state->diagnostics_panel_visible &&
        diagnostics_panel->OnEvent(event)) {
      return true;
    }
    if (tab_bar_state->overflow_open &&
        handle_tabs_overflow_keys(workspace, focus, tab_bar_state.get(), event)) {
      return true;
    }
    if (dispatch_editor_mouse(event)) {
      return true;
    }
    return dispatch_editor_keys(event);
  }));
}

}  // namespace tgdb
