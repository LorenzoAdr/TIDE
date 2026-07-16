#include "editor/editor_state.hpp"
#include "editor/visual_highlight.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <mutex>
#include <utility>
#include <vector>

#include "editor/editor_buffer_source.hpp"
#include "editor/text_search.hpp"
#include "indexer/index_rules.hpp"
#include "parser/tree_sitter_blocks.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_service.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

namespace {

int64_t steady_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool visual_highlight_configs_match(const VisualHighlightConfig& a,
                                    const VisualHighlightConfig& b) {
  return a.enabled == b.enabled && a.brace_pair_colors == b.brace_pair_colors &&
         a.matching_bracket == b.matching_bracket &&
         a.scope_background == b.scope_background &&
         a.scope_brace_highlight == b.scope_brace_highlight &&
         a.scope_strength == b.scope_strength && a.sticky_scroll == b.sticky_scroll &&
         a.diagnostic_suffixes == b.diagnostic_suffixes &&
         a.overview_ruler == b.overview_ruler &&
         a.selection_occurrences == b.selection_occurrences &&
         a.code_folding == b.code_folding;
}

void schedule_visual_highlight_debounce_wake(VisualHighlightPanelState* state, int64_t now_ms) {
  if (state == nullptr || !state->dirty || state->job_inflight) {
    return;
  }
  const int64_t elapsed = now_ms - state->dirty_ms;
  if (elapsed >= kVisualHighlightDebounceMs) {
    return;
  }
  auto& service = visual_highlight_service();
  if (service.debounce_wake_scheduled()) {
    return;
  }
  service.mark_debounce_wake_scheduled();
  service.request_wake_after(kVisualHighlightDebounceMs - elapsed);
}

}  // namespace

VisualHighlightConfig visual_highlight_config_from_settings(const AppSettings* settings) {
  VisualHighlightConfig config;
  if (settings == nullptr) {
    return config;
  }
  config.enabled = settings->visual_highlight_enabled;
  config.brace_pair_colors = settings->visual_brace_pair_colors_enabled;
  config.matching_bracket = settings->visual_matching_bracket_enabled;
  config.scope_background = settings->visual_scope_background_enabled;
  config.scope_brace_highlight = settings->visual_scope_brace_highlight_enabled;
  config.scope_strength = settings->scope_highlight_strength;
  config.sticky_scroll = settings->sticky_scroll_enabled;
  config.diagnostic_suffixes = settings->show_diagnostic_suffixes;
  config.overview_ruler = settings->overview_ruler_enabled;
  config.selection_occurrences = settings->visual_selection_occurrences_enabled;
  config.code_folding = settings->visual_code_folding_enabled;
  return config;
}

VisualHighlightSelectionKey visual_highlight_selection_key_from(const EditorBuffer& buffer) {
  VisualHighlightSelectionKey key;
  // Occurrence highlights only apply with a single cursor; multicursor uses
  // Selection decorations (hashed per-line in the editor viewport cache).
  if (buffer.cursors.size() == 1 && buffer.primary().has_selection()) {
    key.view_token = buffer.view_token;
    buffer.primary().normalized_range(&key.start_line, &key.start_col, &key.end_line, &key.end_col);
  }
  return key;
}

bool selection_needle_is_identifier(const std::string& needle) {
  return !needle.empty() && is_ident_start(needle[0]) &&
         std::all_of(needle.begin(), needle.end(), [](unsigned char c) {
           return is_ident_char(static_cast<char>(c));
         });
}

bool selection_needle_is_valid(const std::string& needle) {
  constexpr std::size_t kMaxNeedle = 256;
  if (needle.size() < 2 || needle.size() > kMaxNeedle) {
    return false;
  }
  return !std::all_of(needle.begin(), needle.end(),
                      [](unsigned char c) { return std::isspace(static_cast<unsigned char>(c)); });
}

VisualHighlightSelectionQuery build_selection_query(const EditorBuffer& buffer) {
  VisualHighlightSelectionQuery query;
  query.key = visual_highlight_selection_key_from(buffer);
  if (query.key.start_line < 0 || buffer.cursors.size() != 1 ||
      !buffer.primary().has_selection() || query.key.start_line != query.key.end_line) {
    return query;
  }

  const std::string needle = selection_text(buffer, buffer.primary());
  if (!selection_needle_is_valid(needle)) {
    return query;
  }

  query.active = true;
  query.needle = needle;
  query.whole_word = selection_needle_is_identifier(needle);
  return query;
}

std::vector<TextMatch> filter_matches_to_viewport(const std::vector<TextMatch>& matches,
                                                  int viewport_scroll, int viewport_visible_lines) {
  if (viewport_visible_lines <= 0 || matches.empty()) {
    return {};
  }
  const int viewport_end = viewport_scroll + viewport_visible_lines;
  std::vector<TextMatch> filtered;
  filtered.reserve(matches.size());
  for (const TextMatch& match : matches) {
    if (match.line >= viewport_scroll && match.line < viewport_end) {
      filtered.push_back(match);
    }
  }
  return filtered;
}

void prune_invalid_collapsed_folds(EditorBuffer* buffer) {
  if (buffer == nullptr) {
    return;
  }
  std::set<int> valid_open_lines;
  for (const FoldRegion& region : buffer->fold_regions) {
    valid_open_lines.insert(region.open_line);
  }
  for (auto it = buffer->collapsed_folds.begin(); it != buffer->collapsed_folds.end();) {
    if (valid_open_lines.count(*it) == 0) {
      it = buffer->collapsed_folds.erase(it);
    } else {
      ++it;
    }
  }
}

VisualHighlightService& VisualHighlightService::instance() {
  static VisualHighlightService service;
  return service;
}

VisualHighlightService::VisualHighlightService() {
  worker_ = std::thread([this] {
    set_current_thread_name("vh-compute");
    worker_main();
  });
  wake_timer_ = std::thread([this] {
    set_current_thread_name("vh-wake");
    wake_timer_main();
  });
}

VisualHighlightService::~VisualHighlightService() {
  stop_.store(true, std::memory_order_release);
  jobs_.close();
  {
    std::lock_guard<std::mutex> lock(wake_timer_mutex_);
    wake_timer_fire_at_ms_ = 0;
  }
  wake_timer_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  if (wake_timer_.joinable()) {
    wake_timer_.join();
  }
}

void VisualHighlightService::set_result_wake_callback(VisualHighlightWakeCallback callback) {
  result_wake_callback_ = std::move(callback);
}

void VisualHighlightService::set_debounce_wake_callback(VisualHighlightWakeCallback callback) {
  debounce_wake_callback_ = std::move(callback);
}

bool VisualHighlightService::debounce_wake_scheduled() const {
  return debounce_wake_scheduled_.load(std::memory_order_acquire);
}

void VisualHighlightService::mark_debounce_wake_scheduled() {
  debounce_wake_scheduled_.store(true, std::memory_order_release);
}

void VisualHighlightService::clear_debounce_wake_scheduled() {
  debounce_wake_scheduled_.store(false, std::memory_order_release);
}

void VisualHighlightService::begin_sync_result_wait(uint64_t generation) {
  sync_wait_generation_.store(generation, std::memory_order_release);
}

bool VisualHighlightService::wait_for_pending_result(uint64_t generation) {
  std::unique_lock<std::mutex> lock(result_sync_mutex_);
  result_sync_cv_.wait_for(lock, std::chrono::milliseconds(3000), [this, generation] {
    return completed_generation_ >= generation || stop_.load(std::memory_order_acquire);
  });
  const bool ready = completed_generation_ >= generation;
  sync_wait_generation_.store(0, std::memory_order_release);
  return ready;
}

void VisualHighlightService::request_completion_wake(uint64_t generation) {
  completion_wake_generation_.store(generation, std::memory_order_release);
}

void VisualHighlightService::clear_completion_wake(uint64_t generation) {
  if (completion_wake_generation_.load(std::memory_order_acquire) == generation) {
    completion_wake_generation_.store(0, std::memory_order_release);
  }
}

bool VisualHighlightService::consume_debounce_due() {
  return debounce_due_.exchange(false, std::memory_order_acq_rel);
}

void VisualHighlightService::request_wake_after(int64_t delay_ms) {
  if (delay_ms < 0) {
    delay_ms = 0;
  }
  const int64_t fire_at = steady_now_ms() + delay_ms;
  {
    std::lock_guard<std::mutex> lock(wake_timer_mutex_);
    // Extend debounce on cursor slide: keep the latest (furthest) deadline.
    if (wake_timer_fire_at_ms_ == 0 || fire_at > wake_timer_fire_at_ms_) {
      wake_timer_fire_at_ms_ = fire_at;
    }
  }
  wake_timer_cv_.notify_one();
}

void VisualHighlightService::wake_timer_main() {
  while (!stop_.load(std::memory_order_acquire)) {
    int64_t fire_at = 0;
    {
      std::unique_lock<std::mutex> lock(wake_timer_mutex_);
      wake_timer_cv_.wait(lock, [this] {
        return stop_.load(std::memory_order_acquire) || wake_timer_fire_at_ms_ != 0;
      });
      if (stop_.load(std::memory_order_acquire)) {
        break;
      }
      fire_at = wake_timer_fire_at_ms_;
      const int64_t now = steady_now_ms();
      if (fire_at > now) {
        wake_timer_cv_.wait_for(lock, std::chrono::milliseconds(fire_at - now), [this, fire_at] {
          return stop_.load(std::memory_order_acquire) || wake_timer_fire_at_ms_ != fire_at;
        });
      }
      if (wake_timer_fire_at_ms_ == fire_at) {
        wake_timer_fire_at_ms_ = 0;
      } else {
        continue;
      }
    }
    debounce_due_.store(true, std::memory_order_release);
    clear_debounce_wake_scheduled();
    if (debounce_wake_callback_) {
      debounce_wake_callback_();
    }
  }
}

void VisualHighlightService::enqueue(VisualHighlightJob job) {
  if (job.path.empty() || job.source.empty()) {
    return;
  }
  jobs_.remove_if([&](const VisualHighlightJob& queued) { return queued.path == job.path; });
  jobs_.push(std::move(job));
}

std::vector<VisualHighlightSnapshot> VisualHighlightService::drain_results() {
  std::vector<VisualHighlightSnapshot> drained;
  while (auto item = results_.try_pop()) {
    drained.push_back(std::move(*item));
  }
  return drained;
}

namespace {

void build_overview_snapshot(const VisualHighlightJob& job, VisualHighlightSnapshot* snap) {
  if (snap == nullptr || !job.config.overview_ruler) {
    return;
  }
  snap->overview.total_lines = job.inputs.total_lines;
  snap->overview.git_changed_lines = job.inputs.git_changed_lines;
  snap->overview.git_untracked_all = job.inputs.git_untracked_all;
  snap->overview.text_matches = job.inputs.text_matches;
}

}  // namespace

VisualHighlightSnapshot VisualHighlightService::compute(const VisualHighlightJob& job) const {
  VisualHighlightSnapshot snap;
  snap.generation = job.generation;
  snap.path = job.path;
  snap.cursor_line = job.cursor_line;
  snap.cursor_col = job.cursor_col;
  snap.doc_revision = job.doc_revision;

  if (!job.config.enabled) {
    return snap;
  }

  const auto tree_snapshot = tree_sitter_service().snapshot_for_highlight(
      job.path, job.source, job.doc_revision);
  TSNode root = {};
  if (tree_snapshot.ok && tree_snapshot.tree_copy != nullptr) {
    root = ts_tree_root_node(tree_snapshot.tree_copy);
    if (job.config.matching_bracket && !ts_node_is_null(root)) {
      snap.matching_bracket =
          bracket_pair_at(root, job.source, job.cursor_line, job.cursor_col);
    }
    if (job.config.scope_background && !tree_snapshot.scope_symbols.empty()) {
      snap.immediate_scope = innermost_scope_range_from_symbols(
          tree_snapshot.scope_symbols, job.cursor_line, job.cursor_col);
    }
    if (job.config.scope_brace_highlight && !ts_node_is_null(root)) {
      snap.scope_braces =
          scope_bracket_pair_from_tree(root, job.source, job.cursor_line, job.cursor_col);
    }
    if (job.config.brace_pair_colors && !ts_node_is_null(root)) {
      snap.colored_braces = colored_curly_braces(root, job.source);
    }
    if (job.config.code_folding && job.recompute_fold_regions && !ts_node_is_null(root)) {
      snap.fold_regions = fold_regions_from_tree(root, job.source);
      snap.fold_regions_revision = job.doc_revision;
    }
  }

  if (job.config.selection_occurrences && job.selection.active && !job.lines.empty()) {
    snap.selection_key = job.selection.key;
    snap.selection_occurrences =
        find_occurrences_in_lines(job.lines, job.selection.needle, job.selection.whole_word,
                                  nullptr, 0);
  } else {
    snap.selection_key = {};
    snap.selection_occurrences.clear();
  }

  if (!job.config.code_folding) {
    snap.fold_regions.clear();
    snap.fold_regions_revision = 0;
  }

  build_overview_snapshot(job, &snap);

  if (tree_snapshot.tree_copy != nullptr) {
    ts_tree_delete(tree_snapshot.tree_copy);
  }
  snap.ready = true;
  return snap;
}

void VisualHighlightService::worker_main() {
  while (true) {
    auto job = jobs_.wait_pop();
    if (!job || stop_.load(std::memory_order_acquire)) {
      break;
    }
    VisualHighlightJob latest = std::move(*job);
    while (auto newer = jobs_.try_pop()) {
      latest = std::move(*newer);
    }
    VisualHighlightSnapshot snap = compute(latest);
    results_.push(snap);
    {
      std::lock_guard<std::mutex> lock(result_sync_mutex_);
      if (snap.generation > completed_generation_) {
        completed_generation_ = snap.generation;
      }
    }
    result_sync_cv_.notify_all();
    const uint64_t sync_gen = sync_wait_generation_.load(std::memory_order_acquire);
    const uint64_t completion_wake = completion_wake_generation_.load(std::memory_order_acquire);
    const bool suppress_wake = snap.ready && sync_gen == snap.generation;
    if (result_wake_callback_ && snap.ready && !suppress_wake &&
        completion_wake == snap.generation) {
      completion_wake_generation_.store(0, std::memory_order_release);
      result_wake_callback_();
    }
  }
}

void mark_visual_highlight_content_dirty(VisualHighlightPanelState* state, int64_t now_ms) {
  if (state == nullptr) {
    return;
  }
  state->dirty = true;
  state->dirty_ms = now_ms;
  // Cursor/viewport highlights need a fresh job; file-wide metadata (sticky symbols,
  // suffixes, overview) stays visible until the next snapshot lands.
  state->snapshot.ready = false;
  visual_highlight_service().clear_debounce_wake_scheduled();
}

void mark_visual_highlight_inputs_dirty(VisualHighlightPanelState* state, int64_t now_ms) {
  if (state == nullptr) {
    return;
  }
  state->dirty = true;
  state->dirty_ms = now_ms;
  visual_highlight_service().clear_debounce_wake_scheduled();
}

void mark_visual_highlight_cursor_dirty(VisualHighlightPanelState* state, int64_t now_ms) {
  if (state == nullptr) {
    return;
  }
  state->dirty = true;
  state->dirty_ms = now_ms;
  state->snapshot.ready = false;
}

void mark_visual_highlight_dirty(VisualHighlightPanelState* state, int64_t now_ms) {
  mark_visual_highlight_content_dirty(state, now_ms);
}

void tick_visual_highlight_scheduler(VisualHighlightPanelState* state, const EditorBuffer& buffer,
                                     const VisualHighlightConfig& config, bool editor_focused,
                                     bool indexed_source, bool content_settled, int64_t now_ms,
                                     const VisualHighlightJobInputs& inputs,
                                     bool selection_in_progress) {
  if (state == nullptr || buffer.path.empty()) {
    return;
  }

  if (!config.enabled) {
    if (state->snapshot.ready || state->job_inflight) {
      state->snapshot = {};
      state->job_inflight = false;
      state->has_last_job_config = false;
    }
    return;
  }

  // Full-buffer join + occurrence scan on huge files can OOM (often SIGKILL, no crash log).
  constexpr int kMaxVisualHighlightLines = 40000;
  if (static_cast<int>(buffer.lines.size()) > kMaxVisualHighlightLines) {
    if (state->job_inflight || state->dirty || state->snapshot.ready) {
      state->snapshot = {};
      state->job_inflight = false;
      state->dirty = false;
      state->has_last_job_config = false;
    }
    return;
  }

  const uint64_t ts_revision = tree_sitter_service().revision_for(buffer.path);
  if (ts_revision != state->last_seen_ts_revision) {
    state->last_seen_ts_revision = ts_revision;
    state->last_fold_attempt_revision = 0;
    if (!state->dirty && !state->job_inflight) {
      mark_visual_highlight_content_dirty(state, now_ms);
    }
  }

  if (state->has_last_job_config &&
      !visual_highlight_configs_match(state->last_job_config, config)) {
    mark_visual_highlight_content_dirty(state, now_ms);
  }

  const int line = buffer.primary_line();
  const int col = buffer.primary_col();
  if (line != state->last_cursor_line || col != state->last_cursor_col) {
    state->last_cursor_line = line;
    state->last_cursor_col = col;
    if (!state->dirty) {
      mark_visual_highlight_cursor_dirty(state, now_ms);
    } else if (!state->job_inflight) {
      state->dirty_ms = now_ms;
      visual_highlight_service().clear_debounce_wake_scheduled();
      schedule_visual_highlight_debounce_wake(state, now_ms);
    }
  }

  if (config.selection_occurrences && !selection_in_progress) {
    const VisualHighlightSelectionKey selection_key = visual_highlight_selection_key_from(buffer);
    if (selection_key != state->last_selection_key) {
      state->last_selection_key = selection_key;
      if (!state->dirty) {
        mark_visual_highlight_cursor_dirty(state, now_ms);
      } else if (!state->job_inflight) {
        state->dirty_ms = now_ms;
        visual_highlight_service().clear_debounce_wake_scheduled();
        schedule_visual_highlight_debounce_wake(state, now_ms);
      }
    }
  } else if (config.selection_occurrences && selection_in_progress) {
    state->last_selection_key = visual_highlight_selection_key_from(buffer);
  } else {
    const VisualHighlightSelectionKey selection_key = visual_highlight_selection_key_from(buffer);
    if (selection_key != state->last_selection_key) {
      state->last_selection_key = selection_key;
    }
    if (!state->snapshot.selection_occurrences.empty()) {
      state->snapshot.selection_key = {};
      state->snapshot.selection_occurrences.clear();
    }
  }

  const bool debounce_due = visual_highlight_service().consume_debounce_due();

  if (!state->dirty || state->job_inflight) {
    if (state->dirty && !state->job_inflight && !debounce_due) {
      schedule_visual_highlight_debounce_wake(state, now_ms);
    }
    return;
  }
  if (!debounce_due) {
    const int64_t debounce_elapsed = now_ms - state->dirty_ms;
    if (debounce_elapsed < kVisualHighlightDebounceMs) {
      schedule_visual_highlight_debounce_wake(state, now_ms);
      return;
    }
  }

  const std::string source = join_editor_lines(buffer.lines);
  const std::string canonical = normalize_editor_source(source);
  if (canonical.empty()) {
    return;
  }

  const uint64_t doc_revision = tree_sitter_service().revision_for(buffer.path);
  VisualHighlightJob job;
  job.generation = state->next_generation++;
  job.path = buffer.path;
  job.source = canonical;
  job.lines = buffer.lines.to_vector();
  job.cursor_line = line;
  job.cursor_col = col;
  job.doc_revision = doc_revision;
  job.config = config;
  job.inputs = inputs;
  job.selection = config.selection_occurrences ? build_selection_query(buffer)
                                               : VisualHighlightSelectionQuery{};
  job.indexed_source = indexed_source;
  job.recompute_fold_regions =
      config.code_folding && indexed_source && content_settled &&
      doc_revision != state->last_applied_fold_revision &&
      doc_revision != state->last_fold_attempt_revision;
  if (job.recompute_fold_regions) {
    state->last_fold_attempt_revision = doc_revision;
  }

  state->pending_generation = job.generation;
  state->pending_path = job.path;
  state->pending_cursor_line = line;
  state->pending_cursor_col = col;
  state->pending_doc_revision = doc_revision;
  state->job_inflight = true;
  state->dirty = false;
  state->last_job_config = config;
  state->has_last_job_config = true;
  visual_highlight_service().clear_debounce_wake_scheduled();

  if (!editor_focused) {
    job.config.matching_bracket = false;
    job.config.scope_brace_highlight = false;
    job.config.selection_occurrences = false;
  }

  const uint64_t dispatched_gen = job.generation;
  visual_highlight_service().begin_sync_result_wait(dispatched_gen);
  visual_highlight_service().enqueue(std::move(job));
}

bool drain_visual_highlight_results(VisualHighlightPanelState* state, const EditorBuffer& buffer,
                                    MainLayoutState* layout, bool editor_focused) {
  if (state == nullptr) {
    return false;
  }

  std::vector<VisualHighlightSnapshot> snaps = visual_highlight_service().drain_results();
  if (snaps.empty()) {
    return false;
  }

  VisualHighlightSnapshot* best = nullptr;
  for (VisualHighlightSnapshot& snap : snaps) {
    if (snap.path != buffer.path || snap.generation != state->pending_generation) {
      continue;
    }
    best = &snap;
  }

  if (best == nullptr) {
    return false;
  }

  state->job_inflight = false;

  VisualHighlightSnapshot merged = std::move(*best);

  if (!editor_focused) {
    merged.matching_bracket = {};
    merged.scope_braces = {};
    merged.selection_occurrences.clear();
    merged.selection_key = {};
  }

  const bool cursor_match =
      merged.cursor_line == buffer.primary_line() && merged.cursor_col == buffer.primary_col();
  const uint64_t current_rev = tree_sitter_service().revision_for(buffer.path);
  const bool revision_match = merged.doc_revision == current_rev;
  const bool track_selection =
      state->has_last_job_config && state->last_job_config.selection_occurrences;
  const VisualHighlightSelectionKey current_selection = visual_highlight_selection_key_from(buffer);
  const bool selection_match =
      !track_selection || merged.selection_key == current_selection;
  if (!cursor_match) {
    merged.matching_bracket = state->snapshot.matching_bracket;
    merged.scope_braces = state->snapshot.scope_braces;
    merged.immediate_scope = state->snapshot.immediate_scope;
  }
  if (!track_selection) {
    merged.selection_key = {};
    merged.selection_occurrences.clear();
  } else if (!selection_match) {
    merged.selection_key = state->snapshot.selection_key;
    merged.selection_occurrences = state->snapshot.selection_occurrences;
  }
  if (merged.fold_regions_revision == 0) {
    merged.fold_regions = state->snapshot.fold_regions;
    merged.fold_regions_revision = state->snapshot.fold_regions_revision;
  }

  merged.ready = revision_match && cursor_match && selection_match;
  state->snapshot = std::move(merged);
  visual_highlight_service().clear_completion_wake(state->pending_generation);

  if (!revision_match || (editor_focused && !cursor_match)) {
    state->dirty = true;
    state->dirty_ms = steady_now_ms();
  } else {
    state->dirty = false;
  }

  if (layout != nullptr && state->snapshot.ready) {
    invalidate_editor_view(layout);
  }
  return true;
}

const std::vector<TextMatch>* visual_highlight_selection_occurrences(
    const VisualHighlightPanelState& state, const EditorBuffer& buffer, bool find_bar_open,
    int viewport_scroll, int viewport_visible_lines) {
  if (find_bar_open || state.snapshot.selection_occurrences.empty()) {
    return nullptr;
  }
  // With multicursor, Selection decorations already cover each match; scanning/
  // filtering occurrences every paint is O(cursors × matches) and causes lag.
  if (buffer.cursors.size() > 1) {
    return nullptr;
  }
  if (state.snapshot.selection_key != visual_highlight_selection_key_from(buffer)) {
    return nullptr;
  }

  thread_local std::vector<TextMatch> viewport_matches;
  viewport_matches = filter_matches_to_viewport(state.snapshot.selection_occurrences,
                                                  viewport_scroll, viewport_visible_lines);
  if (viewport_matches.empty()) {
    return nullptr;
  }
  return &viewport_matches;
}

bool apply_visual_highlight_fold_regions(EditorBuffer* buffer, VisualHighlightPanelState* state,
                                         const VisualHighlightConfig& config, bool indexed_source) {
  if (buffer == nullptr || state == nullptr) {
    return false;
  }
  if (!config.enabled || !config.code_folding || !indexed_source) {
    const bool changed = !buffer->fold_regions.empty() || !buffer->collapsed_folds.empty();
    buffer->fold_regions.clear();
    buffer->collapsed_folds.clear();
    state->last_applied_fold_revision = 0;
    state->last_fold_attempt_revision = 0;
    return changed;
  }

  const VisualHighlightSnapshot& snap = state->snapshot;
  if (snap.fold_regions_revision == 0) {
    return false;
  }
  const uint64_t current_rev = tree_sitter_service().revision_for(buffer->path);
  if (snap.fold_regions_revision != current_rev) {
    return false;
  }

  bool changed = false;
  if (buffer->fold_regions != snap.fold_regions) {
    buffer->fold_regions = snap.fold_regions;
    prune_invalid_collapsed_folds(buffer);
    changed = true;
  }
  if (state->last_applied_fold_revision != snap.fold_regions_revision) {
    state->last_applied_fold_revision = snap.fold_regions_revision;
  }
  return changed;
}

}  // namespace tgdb
