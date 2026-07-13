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
#include "editor/editor_folds.hpp"
#include "editor/text_search.hpp"
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
  bool selection_occurrences = true;
  bool code_folding = true;
};

struct VisualHighlightSelectionKey {
  int start_line = -1;
  int start_col = 0;
  int end_line = 0;
  int end_col = 0;
  uint64_t view_token = 0;

  bool operator==(const VisualHighlightSelectionKey& other) const {
    return start_line == other.start_line && start_col == other.start_col &&
           end_line == other.end_line && end_col == other.end_col &&
           view_token == other.view_token;
  }

  bool operator!=(const VisualHighlightSelectionKey& other) const {
    return !(*this == other);
  }
};

VisualHighlightSelectionKey visual_highlight_selection_key_from(const EditorBuffer& buffer);

VisualHighlightConfig visual_highlight_config_from_settings(const AppSettings* settings);

struct VisualHighlightOverviewData {
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
  VisualHighlightOverviewData overview;
  VisualHighlightSelectionKey selection_key;
  std::vector<TextMatch> selection_occurrences;
  std::vector<FoldRegion> fold_regions;
  uint64_t fold_regions_revision = 0;
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
  VisualHighlightSelectionKey last_selection_key;
  uint64_t last_seen_ts_revision = 0;
  VisualHighlightConfig last_job_config;
  bool has_last_job_config = false;
  uint64_t last_applied_fold_revision = 0;
  uint64_t last_fold_attempt_revision = 0;
};

struct VisualHighlightJobInputs {
  int code_width = 0;
  int total_lines = 0;
  int viewport_scroll = 0;
  int viewport_visible_lines = 0;
  std::unordered_set<int> git_changed_lines;
  bool git_untracked_all = false;
  std::vector<TextMatch> text_matches;
};

struct VisualHighlightSelectionQuery {
  bool active = false;
  std::string needle;
  bool whole_word = false;
  VisualHighlightSelectionKey key;
};

struct VisualHighlightJob {
  uint64_t generation = 0;
  std::string path;
  std::string source;
  std::vector<std::string> lines;
  int cursor_line = 0;
  int cursor_col = 0;
  uint64_t doc_revision = 0;
  VisualHighlightConfig config;
  VisualHighlightJobInputs inputs;
  VisualHighlightSelectionQuery selection;
  bool recompute_fold_regions = false;
  bool indexed_source = false;
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
  void request_completion_wake(uint64_t generation);
  void clear_completion_wake(uint64_t generation);

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
  std::atomic<uint64_t> completion_wake_generation_{0};
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
                                     bool indexed_source, bool content_settled, int64_t now_ms,
                                     const VisualHighlightJobInputs& inputs);

bool drain_visual_highlight_results(VisualHighlightPanelState* state, const EditorBuffer& buffer,
                                    MainLayoutState* layout, bool editor_focused);

const std::vector<TextMatch>* visual_highlight_selection_occurrences(
    const VisualHighlightPanelState& state, const EditorBuffer& buffer, bool find_bar_open,
    int viewport_scroll, int viewport_visible_lines);

bool apply_visual_highlight_fold_regions(EditorBuffer* buffer, VisualHighlightPanelState* state,
                                         const VisualHighlightConfig& config, bool indexed_source);

inline VisualHighlightService& visual_highlight_service() {
  return VisualHighlightService::instance();
}

}  // namespace tgdb
