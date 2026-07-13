#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/app_settings.hpp"
#include "editor/bracket_match.hpp"
#include "editor/text_search.hpp"
#include "lsp/diagnostics.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "symbols/symbol_provider.hpp"
#include "ui/main_layout.hpp"
#include "util/thread_safe_queue.hpp"

namespace tgdb {

struct EditorBuffer;

using VisualHighlightWakeCallback = std::function<void()>;

constexpr int kVisualHighlightDebounceMs = 200;

struct VisualHighlightConfig {
  bool enabled = true;
  bool brace_pair_colors = true;
  bool matching_bracket = true;
  bool scope_background = true;
  bool scope_brace_highlight = true;
  int scope_strength = 58;
  bool sticky_scroll = true;
  bool diagnostic_suffixes = true;
  bool overview_ruler = true;
};

VisualHighlightConfig visual_highlight_config_from_settings(const AppSettings* settings);

struct VisualHighlightOverviewData {
  std::unordered_map<int, std::vector<Diagnostic>> diagnostics_by_line;
  std::unordered_set<int> git_changed_lines;
  bool git_untracked_all = false;
  std::vector<TextMatch> text_matches;
  int total_lines = 0;
};

struct VisualHighlightSnapshot {
  uint64_t generation = 0;
  std::string path;
  int cursor_line = -1;
  int cursor_col = -1;
  uint64_t doc_revision = 0;
  BracketPairHighlight matching_bracket;
  BracketPairHighlight scope_braces;
  ScopeLineRange immediate_scope;
  std::vector<ColoredBraceMarker> colored_braces;
  std::vector<SymbolInfo> file_symbols;
  std::unordered_map<int, std::string> diagnostic_suffix_by_line;
  int diagnostic_suffix_code_width = 0;
  VisualHighlightOverviewData overview;
  bool ready = false;
};

struct VisualHighlightPanelState {
  VisualHighlightSnapshot snapshot;
  bool dirty = false;
  int64_t dirty_ms = 0;
  bool job_inflight = false;
  uint64_t next_generation = 1;
  uint64_t pending_generation = 0;
  std::string pending_path;
  int pending_cursor_line = -1;
  int pending_cursor_col = -1;
  uint64_t pending_doc_revision = 0;
  int last_cursor_line = -1;
  int last_cursor_col = -1;
  uint64_t last_seen_ts_revision = 0;
  VisualHighlightConfig last_job_config;
  bool has_last_job_config = false;
};

struct VisualHighlightJobInputs {
  int code_width = 0;
  int total_lines = 0;
  bool diagnostics_ui_allowed = false;
  std::unordered_map<int, std::vector<Diagnostic>> diagnostics_by_line;
  std::unordered_set<int> git_changed_lines;
  bool git_untracked_all = false;
  std::vector<TextMatch> text_matches;
};

struct VisualHighlightJob {
  uint64_t generation = 0;
  std::string path;
  std::string source;
  int cursor_line = 0;
  int cursor_col = 0;
  uint64_t doc_revision = 0;
  VisualHighlightConfig config;
  VisualHighlightJobInputs inputs;
};

class VisualHighlightService {
 public:
  static VisualHighlightService& instance();

  void enqueue(VisualHighlightJob job);
  std::vector<VisualHighlightSnapshot> drain_results();
  void set_result_wake_callback(VisualHighlightWakeCallback callback);
  void set_debounce_wake_callback(VisualHighlightWakeCallback callback);
  void request_wake_after(int64_t delay_ms);
  bool consume_debounce_due();
  bool debounce_wake_scheduled() const;
  void mark_debounce_wake_scheduled();
  void clear_debounce_wake_scheduled();
  void begin_sync_result_wait(uint64_t generation);
  bool wait_for_pending_result(uint64_t generation);

 private:
  VisualHighlightService();
  ~VisualHighlightService();
  VisualHighlightService(const VisualHighlightService&) = delete;
  VisualHighlightService& operator=(const VisualHighlightService&) = delete;

  void worker_main();
  void wake_timer_main();
  VisualHighlightSnapshot compute(const VisualHighlightJob& job) const;

  ThreadSafeQueue<VisualHighlightJob> jobs_;
  ThreadSafeQueue<VisualHighlightSnapshot> results_;
  std::thread worker_;
  std::thread wake_timer_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> debounce_due_{false};
  std::atomic<bool> debounce_wake_scheduled_{false};
  VisualHighlightWakeCallback result_wake_callback_;
  VisualHighlightWakeCallback debounce_wake_callback_;
  std::mutex result_sync_mutex_;
  std::condition_variable result_sync_cv_;
  std::atomic<uint64_t> sync_wait_generation_{0};
  uint64_t completed_generation_ = 0;
  std::mutex wake_timer_mutex_;
  std::condition_variable wake_timer_cv_;
  int64_t wake_timer_fire_at_ms_ = 0;
};

void mark_visual_highlight_content_dirty(VisualHighlightPanelState* state, int64_t now_ms);
void mark_visual_highlight_inputs_dirty(VisualHighlightPanelState* state, int64_t now_ms);

void mark_visual_highlight_cursor_dirty(VisualHighlightPanelState* state, int64_t now_ms);

void mark_visual_highlight_dirty(VisualHighlightPanelState* state, int64_t now_ms);

void tick_visual_highlight_scheduler(VisualHighlightPanelState* state, const EditorBuffer& buffer,
                                     const VisualHighlightConfig& config, bool editor_focused,
                                     bool indexed_cpp, int64_t now_ms,
                                     const VisualHighlightJobInputs& inputs);

bool drain_visual_highlight_results(VisualHighlightPanelState* state, const EditorBuffer& buffer,
                                    MainLayoutState* layout, bool editor_focused);

inline VisualHighlightService& visual_highlight_service() {
  return VisualHighlightService::instance();
}

}  // namespace tgdb
