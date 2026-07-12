#include "editor/editor_state.hpp"
#include "editor/visual_highlight.hpp"

#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <utility>

#include "editor/editor_buffer_source.hpp"
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
         a.scope_strength == b.scope_strength;
}

// #region agent log
void vh_agent_log(const char* message, const std::string& data) {
  std::ofstream out("/home/lorenzo/workspace/tgdb/.cursor/debug-b2c081.log", std::ios::app);
  if (!out) {
    return;
  }
  const int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  out << "{\"sessionId\":\"b2c081\",\"location\":\"visual_highlight.cpp\",\"message\":\""
      << message << "\",\"data\":" << data << ",\"timestamp\":" << ts << "}\n";
}
// #endregion

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
  return config;
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
  result_sync_cv_.wait_for(lock, std::chrono::milliseconds(100), [this, generation] {
    return completed_generation_ >= generation || stop_.load(std::memory_order_acquire);
  });
  sync_wait_generation_.store(0, std::memory_order_release);
  return completed_generation_ >= generation;
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
  if (!tree_snapshot.ok || tree_snapshot.tree_copy == nullptr) {
    return snap;
  }

  const TSNode root = ts_tree_root_node(tree_snapshot.tree_copy);
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

  ts_tree_delete(tree_snapshot.tree_copy);
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
    const bool suppress_wake = snap.ready && sync_gen == snap.generation;
    if (result_wake_callback_ && snap.ready && !suppress_wake) {
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
  state->snapshot.ready = false;
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
                                     bool indexed_cpp, int64_t now_ms) {
  if (state == nullptr || buffer.path.empty() || !indexed_cpp) {
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

  const uint64_t ts_revision = tree_sitter_service().revision_for(buffer.path);
  if (ts_revision != state->last_seen_ts_revision) {
    state->last_seen_ts_revision = ts_revision;
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
      // #region agent log
      vh_agent_log("debounce_slide", "{\"line\":" + std::to_string(line) + ",\"col\":" +
                                         std::to_string(col) + "}");
      // #endregion
    }
  }

  const bool debounce_due = visual_highlight_service().consume_debounce_due();

  if (!state->dirty || state->job_inflight) {
    if (!debounce_due) {
      schedule_visual_highlight_debounce_wake(state, now_ms);
    }
    return;
  }
  const int64_t debounce_elapsed = now_ms - state->dirty_ms;
  if (debounce_elapsed < kVisualHighlightDebounceMs) {
    if (debounce_due) {
      // #region agent log
      vh_agent_log("stale_timer_ignored",
                   "{\"elapsed\":" + std::to_string(debounce_elapsed) + "}");
      // #endregion
    }
    schedule_visual_highlight_debounce_wake(state, now_ms);
    return;
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
  job.cursor_line = line;
  job.cursor_col = col;
  job.doc_revision = doc_revision;
  job.config = config;

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
  }

  const uint64_t dispatched_gen = job.generation;
  visual_highlight_service().begin_sync_result_wait(dispatched_gen);
  visual_highlight_service().enqueue(std::move(job));
  // #region agent log
  vh_agent_log("job_dispatched", "{\"gen\":" + std::to_string(dispatched_gen) + ",\"elapsed\":" +
                                      std::to_string(debounce_elapsed) + ",\"line\":" +
                                      std::to_string(line) + ",\"col\":" + std::to_string(col) +
                                      "}");
  // #endregion
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
    if (snap.path != buffer.path || snap.generation != state->pending_generation ||
        snap.doc_revision != tree_sitter_service().revision_for(buffer.path)) {
      state->job_inflight = false;
      state->dirty = true;
      state->dirty_ms = steady_now_ms() - kVisualHighlightDebounceMs;
      visual_highlight_service().clear_debounce_wake_scheduled();
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
  }

  const bool cursor_match =
      merged.cursor_line == buffer.primary_line() && merged.cursor_col == buffer.primary_col();
  if (!cursor_match) {
    merged.matching_bracket = state->snapshot.matching_bracket;
    merged.scope_braces = state->snapshot.scope_braces;
    merged.immediate_scope = state->snapshot.immediate_scope;
  }

  state->snapshot = std::move(merged);

  if (layout != nullptr) {
    invalidate_editor_view(layout);
  }
  return true;
}

}  // namespace tgdb
