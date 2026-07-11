#include "parser/tree_sitter_document.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>

#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_language.hpp"
#include "parser/tree_sitter_locals.hpp"
#include "parser/tree_sitter_symbols.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

namespace {

TSPoint byte_offset_to_point(const std::string& source, std::size_t byte_offset) {
  TSPoint point{};
  std::size_t offset = 0;
  for (std::size_t i = 0; i < source.size(); ++i) {
    if (offset >= byte_offset) {
      break;
    }
    if (source[i] == '\n') {
      ++point.row;
      point.column = 0;
    } else {
      ++point.column;
    }
    ++offset;
  }
  return point;
}

std::optional<TSInputEdit> single_edit_between(const std::string& old_source,
                                               const std::string& new_source) {
  if (old_source == new_source) {
    return std::nullopt;
  }
  const std::size_t max_prefix =
      std::min(old_source.size(), new_source.size());
  std::size_t prefix = 0;
  while (prefix < max_prefix && old_source[prefix] == new_source[prefix]) {
    ++prefix;
  }
  if (prefix == old_source.size() && prefix == new_source.size()) {
    return std::nullopt;
  }

  std::size_t old_suffix = old_source.size();
  std::size_t new_suffix = new_source.size();
  while (old_suffix > prefix && new_suffix > prefix &&
         old_source[old_suffix - 1] == new_source[new_suffix - 1]) {
    --old_suffix;
    --new_suffix;
  }
  if (old_suffix < prefix || new_suffix < prefix) {
    return std::nullopt;
  }

  TSInputEdit edit{};
  edit.start_byte = static_cast<uint32_t>(prefix);
  edit.old_end_byte = static_cast<uint32_t>(old_suffix);
  edit.new_end_byte = static_cast<uint32_t>(new_suffix);
  edit.start_point = byte_offset_to_point(old_source, prefix);
  edit.old_end_point = byte_offset_to_point(old_source, old_suffix);
  edit.new_end_point = byte_offset_to_point(new_source, new_suffix);
  return edit;
}

// Above this line-count delta, an edit is treated as a "large" change (big paste,
// multi-line block delete, etc.) and is left to the worker's snapshot path so the
// heavier diffing/highlight work stays off the UI thread. Below it (the common case
// while typing: a single Enter, a backspace joining two lines, autoindent...), we can
// safely apply the incremental edit synchronously below.
constexpr int kSyncIncrementalMaxLineDelta = 1;

int count_source_lines(const std::string& source) {
  if (source.empty()) {
    return 1;
  }
  int lines = 1;
  for (char ch : source) {
    if (ch == '\n') {
      ++lines;
    }
  }
  return lines;
}

// A plain `Enter` splits one line into a prefix (kept at the same index) and a
// suffix that becomes the new line below it. That suffix isn't actually new
// content -- it's the tail of a line tree-sitter already had highlights for --
// so instead of leaving it blank until the async worker re-parses, keep the
// spans that fall after the split column and shift them to their new column.
// `dest_col_offset` accounts for auto-indent: the new line's real content is
// `indent + tail`, not just `tail` starting at column 0, so the tail's spans
// must land at `indent_len` onward, not at 0 -- otherwise they end up shorter
// than the real token and the last characters render with no color at all.
// Spans straddling the split point get clamped rather than dropped, which is
// only ever slightly wrong (at most one token's worth) for the brief window
// until the real reparse lands.
LineHighlights shift_highlights_for_split(const LineHighlights& original, int split_col,
                                          int dest_col_offset) {
  LineHighlights out;
  out.spans.reserve(original.spans.size());
  for (const HighlightSpan& span : original.spans) {
    if (span.end_col <= split_col) {
      continue;
    }
    HighlightSpan shifted = span;
    shifted.start_col = std::max(0, span.start_col - split_col) + dest_col_offset;
    shifted.end_col = span.end_col - split_col + dest_col_offset;
    out.spans.push_back(shifted);
  }
  return out;
}

// Keep highlight indices aligned with buffer lines while the worker re-parses.
void remap_line_highlights_for_count_change(std::vector<LineHighlights>* highlights,
                                            const TSInputEdit& edit, int old_line_count,
                                            int new_line_count) {
  if (highlights == nullptr || old_line_count == new_line_count) {
    return;
  }

  const std::vector<LineHighlights> previous = *highlights;
  const int edit_row = static_cast<int>(edit.start_point.row);

  if (new_line_count > old_line_count) {
    const int added = new_line_count - old_line_count;
    const int insert_at = edit.start_point.column == 0 ? edit_row : edit_row + 1;
    // Only a single-line split (one Enter press, mid-line) has a well-defined
    // "previous content" to approximate from; multi-line pastes/edits that add
    // several lines at once have no prior per-line highlights to borrow, so
    // those stay blank placeholders as before.
    const bool single_line_split = added == 1 && edit.start_point.column > 0 &&
                                   edit_row < static_cast<int>(previous.size());
    // When the insertion is a plain "\n" + indent (the common Enter-with-autoindent
    // case), new_end_point lands one row below start_point, at the column right after
    // the inserted indent -- i.e. exactly the indent's length. Any other shape (e.g. a
    // hint/diff edit that also touched later rows) can't be trusted as an indent
    // length, so fall back to no offset rather than risk shifting to the wrong column.
    const int dest_col_offset = (single_line_split &&
                                 edit.new_end_point.row == static_cast<uint32_t>(edit_row + 1))
                                    ? static_cast<int>(edit.new_end_point.column)
                                    : 0;
    std::vector<LineHighlights> out(static_cast<std::size_t>(new_line_count));
    for (int i = 0; i < new_line_count; ++i) {
      if (i < insert_at) {
        if (i < static_cast<int>(previous.size())) {
          out[static_cast<std::size_t>(i)] = previous[static_cast<std::size_t>(i)];
        }
      } else if (i < insert_at + added) {
        out[static_cast<std::size_t>(i)] =
            single_line_split
                ? shift_highlights_for_split(previous[static_cast<std::size_t>(edit_row)],
                                             static_cast<int>(edit.start_point.column),
                                             dest_col_offset)
                : LineHighlights{};
      } else {
        const int src = i - added;
        if (src >= 0 && src < static_cast<int>(previous.size())) {
          out[static_cast<std::size_t>(i)] = previous[static_cast<std::size_t>(src)];
        }
      }
    }
    *highlights = std::move(out);
    return;
  }

  const int removed = old_line_count - new_line_count;
  const int skip_from = edit.start_point.column == 0 ? edit_row : edit_row + 1;
  std::vector<LineHighlights> out(static_cast<std::size_t>(new_line_count));
  for (int i = 0; i < new_line_count; ++i) {
    int src = i;
    if (edit.start_point.column > 0 && i == edit_row) {
      src = edit_row;
    } else if (i >= skip_from) {
      src = i + removed;
    }
    if (src >= 0 && src < static_cast<int>(previous.size())) {
      out[static_cast<std::size_t>(i)] = previous[static_cast<std::size_t>(src)];
    }
  }
  *highlights = std::move(out);
}

TSTree* parse_source(TSParser* parser, const std::string& source, const std::string& previous_source,
                     TSTree* previous_tree) {
  if (previous_tree != nullptr && !previous_source.empty()) {
    if (const std::optional<TSInputEdit> edit = single_edit_between(previous_source, source)) {
      ts_tree_edit(previous_tree, &*edit);
      TSTree* tree = ts_parser_parse_string(parser, previous_tree, source.c_str(),
                                            static_cast<uint32_t>(source.size()));
      if (tree != nullptr) {
        return tree;
      }
    }
  }
  if (previous_tree != nullptr) {
    TSTree* tree = ts_parser_parse_string(parser, previous_tree, source.c_str(),
                                          static_cast<uint32_t>(source.size()));
    if (tree != nullptr) {
      return tree;
    }
  }
  return ts_parser_parse_string(parser, nullptr, source.c_str(),
                                static_cast<uint32_t>(source.size()));
}

// Converts a buffer-level edit hint (see editor_buffer_source.hpp) into a
// TSInputEdit against `old_canonical`/`new_canonical`, without touching
// either string -- this is the whole point: single_edit_between() below is
// an O(document size) prefix/suffix scan, while this is O(1).
//
// The hint's byte offsets are computed against the buffer's *raw* joined
// source (see join_editor_lines), while old_canonical/new_canonical have
// been through normalize_editor_source(), which strips a single trailing
// '\n' if present. That only matters when the edit's own end reaches the
// byte normalize would strip (i.e. it touches the buffer's last, empty
// line) -- bail out to the (correct, if O(n)) diff fallback in that case
// rather than risk an off-by-one TSInputEdit, which tree-sitter has no way
// to detect as wrong.
std::optional<TSInputEdit> ts_input_edit_from_hint(const EditorTextEditHint& hint,
                                                   const std::string& old_canonical,
                                                   const std::string& new_canonical) {
  if (hint.old_ends_with_newline != hint.new_ends_with_newline) {
    return std::nullopt;
  }
  if (hint.old_end_byte > old_canonical.size() || hint.new_end_byte > new_canonical.size()) {
    return std::nullopt;
  }
  // Belt-and-suspenders (O(1)) consistency check: the hint's implied length
  // delta must exactly match the actual size delta between the two
  // canonical strings. A stale/mismatched hint (e.g. from a code path that
  // doesn't keep old_canonical/new_canonical in lockstep with the hint's
  // originating buffer) would almost certainly fail this.
  const std::ptrdiff_t hint_delta = static_cast<std::ptrdiff_t>(hint.new_end_byte) -
                                    static_cast<std::ptrdiff_t>(hint.old_end_byte);
  const std::ptrdiff_t actual_delta = static_cast<std::ptrdiff_t>(new_canonical.size()) -
                                      static_cast<std::ptrdiff_t>(old_canonical.size());
  if (hint_delta != actual_delta) {
    return std::nullopt;
  }

  TSInputEdit edit{};
  edit.start_byte = static_cast<uint32_t>(hint.start_byte);
  edit.old_end_byte = static_cast<uint32_t>(hint.old_end_byte);
  edit.new_end_byte = static_cast<uint32_t>(hint.new_end_byte);
  edit.start_point = TSPoint{static_cast<uint32_t>(hint.start_row), static_cast<uint32_t>(hint.start_col)};
  edit.old_end_point =
      TSPoint{static_cast<uint32_t>(hint.old_end_row), static_cast<uint32_t>(hint.old_end_col)};
  edit.new_end_point =
      TSPoint{static_cast<uint32_t>(hint.new_end_row), static_cast<uint32_t>(hint.new_end_col)};
  return edit;
}

bool apply_sync_source_edit(DocumentEntry* entry, const std::string& canonical,
                            const std::optional<TSInputEdit>& hint_edit) {
  if (entry == nullptr || entry->tree == nullptr || entry->source == canonical) {
    return false;
  }
  const std::optional<TSInputEdit> edit =
      hint_edit.has_value() ? hint_edit : single_edit_between(entry->source, canonical);
  if (!edit.has_value()) {
    return false;
  }

  const int old_line_count = count_source_lines(entry->source);
  const int new_line_count = count_source_lines(canonical);
  const int line_delta = new_line_count > old_line_count ? new_line_count - old_line_count
                                                          : old_line_count - new_line_count;

  if (line_delta > kSyncIncrementalMaxLineDelta) {
    // Large layout change: defer to the worker so the UI thread never runs the
    // heavier diff/highlight work, and so it can keep the pre-edit tree around for
    // proper incremental-highlight diffing (see request_prepare's snapshot path).
    return false;
  }

  // ts_tree_edit() only marks the affected byte/point range dirty -- it never
  // reparses -- so it's just as cheap for edits that change the line count (a single
  // Enter, a backspace joining two lines, autoindent...) as it is for same-line edits.
  // Applying it here, synchronously, lets the entry keep its live tree (parse_ready
  // stays true) instead of nulling it out and bouncing the whole thing through the
  // debounce snapshot machinery below, purely because the line count moved by one.
  ts_tree_edit(entry->tree, &*edit);
  entry->source = canonical;
  if (!entry->line_highlights.empty()) {
    remap_line_highlights_for_count_change(&entry->line_highlights, *edit, old_line_count,
                                           new_line_count);
  }
  entry->parse_ready = true;
  entry->highlights_ready = false;
  entry->symbols_ready = false;
  return true;
}

}  // namespace

std::string join_editor_lines(const std::vector<std::string>& lines) {
  std::string out;
  std::size_t total = 0;
  for (const std::string& line : lines) {
    total += line.size() + 1;
  }
  out.reserve(total);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) {
      out.push_back('\n');
    }
    out += lines[i];
  }
  return out;
}

std::string normalize_editor_source(const std::string& source) {
  // This used to split the whole source into a fresh vector<string> via
  // istringstream::getline (one allocation per line) and immediately rejoin
  // it, purely to drop a single trailing '\n' if present -- getline never
  // strips embedded '\r', so that's the entire observable effect. It was
  // called on every keystroke (via cursor_in_code's auto-pair check), making
  // it an O(document size) cost -- with a large per-call constant on top,
  // since it used to be   O(document size) *and* allocation-heavy -- paid on
  // every single character typed. A single substr() is behaviorally
  // identical and just as O(n) but without the per-line allocations/stream
  // overhead.
  if (!source.empty() && source.back() == '\n') {
    return source.substr(0, source.size() - 1);
  }
  return source;
}

std::string join_editor_lines_from_file(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return join_editor_lines(lines);
}

DocumentEntry::~DocumentEntry() {
  if (tree != nullptr) {
    ts_tree_delete(tree);
    tree = nullptr;
  }
}

TreeSitterDocumentCache::TreeSitterDocumentCache() {
  worker_ = std::thread([this] {
    set_current_thread_name("ts-parse");
    worker_main();
  });
}

TreeSitterDocumentCache::~TreeSitterDocumentCache() {
  {
    std::lock_guard<std::mutex> lock(worker_mutex_);
    worker_stop_ = true;
  }
  worker_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

void TreeSitterDocumentCache::set_ready_callback(ReadyCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  ready_callback_ = std::move(callback);
}

void TreeSitterDocumentCache::reset_entry(DocumentEntry* entry) {
  if (entry == nullptr) {
    return;
  }
  if (entry->tree != nullptr) {
    ts_tree_delete(entry->tree);
    entry->tree = nullptr;
  }
  entry->line_highlights.clear();
  entry->symbols.clear();
  entry->scope_symbols.clear();
  entry->parse_ready = false;
  entry->highlights_ready = false;
  entry->symbols_ready = false;
  entry->prepare_inflight = false;
}

DocumentPtr TreeSitterDocumentCache::lookup(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = cache_.find(path);
  if (it == cache_.end()) {
    return nullptr;
  }
  return it->second;
}

void TreeSitterDocumentCache::request_prepare(const std::string& path, const std::string& source,
                                              const std::optional<EditorTextEditHint>& edit_hint) {
  const std::string canonical = normalize_editor_source(source);
  if (path.empty() || canonical.empty()) {
    return;
  }

  DebouncedPrepare debounced;
  debounced.path = path;
  debounced.source = canonical;
  bool cold_parse = false;
  bool source_changed = false;
  bool should_enqueue = true;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    DocumentPtr& entry = cache_[path];
    if (entry == nullptr) {
      entry = std::make_shared<DocumentEntry>();
      entry->path = path;
    }

    source_changed = entry->source != canonical;
    if (source_changed) {
      const int old_line_count = count_source_lines(entry->source);
      const int new_line_count = count_source_lines(canonical);
      const std::optional<TSInputEdit> hint_edit =
          edit_hint.has_value() ? ts_input_edit_from_hint(*edit_hint, entry->source, canonical)
                                : std::nullopt;
      const std::optional<TSInputEdit> layout_edit =
          old_line_count != new_line_count
              ? (hint_edit.has_value() ? hint_edit : single_edit_between(entry->source, canonical))
              : std::nullopt;
      if (apply_sync_source_edit(entry.get(), canonical, hint_edit)) {
        if (entry->highlights_ready) {
          entry->revision = next_revision_++;
        }
      } else {
        debounced.previous_source = entry->source;
        debounced.previous_tree = entry->tree;
        entry->tree = nullptr;
        entry->source = canonical;
        if (layout_edit.has_value() && !entry->line_highlights.empty()) {
          remap_line_highlights_for_count_change(&entry->line_highlights, *layout_edit,
                                                 old_line_count, new_line_count);
        } else {
          entry->line_highlights.clear();
        }
        entry->parse_ready = false;
        entry->highlights_ready = false;
        entry->symbols_ready = false;
      }
    }

    if (entry->parse_ready && entry->highlights_ready && entry->symbols_ready &&
        entry->source == canonical) {
      if (debounced.previous_tree != nullptr) {
        ts_tree_delete(debounced.previous_tree);
      }
      return;
    }

    if (!source_changed) {
      if (entry->prepare_inflight) {
        should_enqueue = false;
      }
    }

    if (should_enqueue) {
      cold_parse = entry->tree == nullptr && debounced.previous_tree == nullptr;
      debounced.run_after =
          std::chrono::steady_clock::now() +
          (cold_parse ? std::chrono::milliseconds(0)
                      : std::chrono::milliseconds(kTreeSitterParseDebounceMs));
    }
  }

  if (!should_enqueue) {
    if (debounced.previous_tree != nullptr) {
      ts_tree_delete(debounced.previous_tree);
    }
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    const auto it = debounce_jobs_.find(path);
    if (it != debounce_jobs_.end() && it->second.source == canonical) {
      return;
    }
    cold_parse = false;
    debounced.run_after = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kTreeSitterParseDebounceMs);
  }

  {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    DebouncedPrepare& pending = debounce_jobs_[path];
    if (pending.previous_tree == nullptr && debounced.previous_tree != nullptr) {
      pending.previous_tree = debounced.previous_tree;
      pending.previous_source = std::move(debounced.previous_source);
      debounced.previous_tree = nullptr;
    } else if (debounced.previous_tree != nullptr) {
      ts_tree_delete(debounced.previous_tree);
    }
    pending.path = path;
    pending.source = std::move(debounced.source);
    if (source_changed || cold_parse || pending.run_after == std::chrono::steady_clock::time_point{}) {
      pending.run_after = debounced.run_after;
    }
  }
  worker_cv_.notify_one();
}

void TreeSitterDocumentCache::invalidate(const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  cache_.erase(path);
}

uint64_t TreeSitterDocumentCache::revision_for(const std::string& path) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = cache_.find(path);
  if (it == cache_.end()) {
    return 0;
  }
  return it->second->revision;
}

void TreeSitterDocumentCache::flush_ready_debounce_jobs(std::vector<PrepareJob>* ready_jobs) {
  if (ready_jobs == nullptr) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  for (auto it = debounce_jobs_.begin(); it != debounce_jobs_.end();) {
    if (it->second.run_after > now) {
      ++it;
      continue;
    }
    PrepareJob job;
    job.path = it->second.path;
    job.source = std::move(it->second.source);
    job.previous_source = std::move(it->second.previous_source);
    job.previous_tree = it->second.previous_tree;
    it->second.previous_tree = nullptr;
    ready_jobs->push_back(std::move(job));
    it = debounce_jobs_.erase(it);
  }
}

void TreeSitterDocumentCache::worker_main() {
  while (true) {
    std::vector<PrepareJob> ready_jobs;
    {
      std::unique_lock<std::mutex> lock(worker_mutex_);
      while (true) {
        if (worker_stop_) {
          for (auto& pending : debounce_jobs_) {
            if (pending.second.previous_tree != nullptr) {
              ts_tree_delete(pending.second.previous_tree);
            }
          }
          debounce_jobs_.clear();
          if (pending_jobs_.empty()) {
            return;
          }
        }

        flush_ready_debounce_jobs(&ready_jobs);
        for (PrepareJob& job : ready_jobs) {
          bool schedule = false;
          {
            std::lock_guard<std::mutex> cache_lock(mutex_);
            const auto it = cache_.find(job.path);
            if (it != cache_.end()) {
              DocumentEntry* entry = it->second.get();
              if (!entry->prepare_inflight && !entry->parse_ready &&
                  entry->source == job.source) {
                entry->prepare_inflight = true;
                schedule = true;
              } else if (entry->prepare_inflight) {
                DebouncedPrepare& retry = debounce_jobs_[job.path];
                if (retry.previous_tree == nullptr && job.previous_tree != nullptr) {
                  retry.previous_tree = job.previous_tree;
                  retry.previous_source = std::move(job.previous_source);
                  job.previous_tree = nullptr;
                } else if (job.previous_tree != nullptr) {
                  ts_tree_delete(job.previous_tree);
                  job.previous_tree = nullptr;
                }
                retry.path = job.path;
                retry.source = std::move(job.source);
                retry.run_after = std::chrono::steady_clock::now();
              } else if (entry->source != job.source) {
                DebouncedPrepare& retry = debounce_jobs_[job.path];
                if (retry.previous_tree == nullptr && job.previous_tree != nullptr) {
                  retry.previous_tree = job.previous_tree;
                  retry.previous_source = std::move(job.previous_source);
                  job.previous_tree = nullptr;
                } else if (job.previous_tree != nullptr) {
                  ts_tree_delete(job.previous_tree);
                  job.previous_tree = nullptr;
                }
                retry.path = job.path;
                retry.source = entry->source;
                const bool cold_parse = retry.previous_tree == nullptr;
                retry.run_after =
                    std::chrono::steady_clock::now() +
                    (cold_parse ? std::chrono::milliseconds(0)
                                : std::chrono::milliseconds(kTreeSitterParseDebounceMs));
              } else if (job.previous_tree != nullptr) {
                ts_tree_delete(job.previous_tree);
                job.previous_tree = nullptr;
              }
            } else if (job.previous_tree != nullptr) {
              ts_tree_delete(job.previous_tree);
              job.previous_tree = nullptr;
            }
          }
          if (schedule) {
            pending_jobs_.push_back(std::move(job));
          }
        }
        ready_jobs.clear();

        if (!pending_jobs_.empty()) {
          break;
        }

        if (worker_stop_ && debounce_jobs_.empty()) {
          return;
        }

        const auto now = std::chrono::steady_clock::now();
        auto next_wake = std::chrono::steady_clock::time_point::max();
        for (const auto& pending : debounce_jobs_) {
          next_wake = std::min(next_wake, pending.second.run_after);
        }
        if (next_wake == std::chrono::steady_clock::time_point::max()) {
          worker_cv_.wait(lock, [this] { return worker_stop_ || !debounce_jobs_.empty(); });
        } else if (next_wake > now) {
          worker_cv_.wait_until(lock, next_wake, [this, next_wake] {
            if (worker_stop_ || !pending_jobs_.empty()) {
              return true;
            }
            const auto wake_now = std::chrono::steady_clock::now();
            for (const auto& pending : debounce_jobs_) {
              if (pending.second.run_after <= wake_now) {
                return true;
              }
            }
            return false;
          });
        } else {
          worker_cv_.wait_for(lock, std::chrono::milliseconds(1));
        }
      }
    }

    PrepareJob job;
    {
      std::lock_guard<std::mutex> lock(worker_mutex_);
      if (pending_jobs_.empty()) {
        continue;
      }
      job = std::move(pending_jobs_.front());
      pending_jobs_.erase(pending_jobs_.begin());
    }
    run_prepare(std::move(job));
  }
}

void TreeSitterDocumentCache::run_prepare(PrepareJob job) {
  TSTree* old_tree_for_highlights = nullptr;
  std::vector<LineHighlights> previous_highlights;
  TSTree* parse_base = job.previous_tree;
  if (parse_base != nullptr) {
    job.previous_tree = nullptr;
  } else {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(job.path);
    if (it != cache_.end() && it->second->tree != nullptr) {
      parse_base = ts_tree_copy(it->second->tree);
      old_tree_for_highlights = ts_tree_copy(it->second->tree);
      previous_highlights = it->second->line_highlights;
    }
  }

  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_cpp_language());
  TSTree* tree = parse_source(parser, job.source, job.previous_source, parse_base);
  if (parse_base != nullptr) {
    ts_tree_delete(parse_base);
    parse_base = nullptr;
  }
  if (job.previous_tree != nullptr) {
    ts_tree_delete(job.previous_tree);
    job.previous_tree = nullptr;
  }
  ts_parser_delete(parser);

  bool more_edits_pending = false;
  {
    std::lock_guard<std::mutex> worker_lock(worker_mutex_);
    more_edits_pending = debounce_jobs_.find(job.path) != debounce_jobs_.end();
  }

  std::vector<LineHighlights> highlights;
  std::vector<SymbolInfo> symbols;
  std::vector<SymbolInfo> scopes;
  if (tree != nullptr) {
    const TSNode root = ts_tree_root_node(tree);
    if (old_tree_for_highlights != nullptr) {
      int layout_shift_from_row = -1;
      if (!job.previous_source.empty() &&
          count_source_lines(job.previous_source) != count_source_lines(job.source)) {
        const std::optional<TSInputEdit> edit =
            single_edit_between(job.previous_source, job.source);
        if (edit.has_value()) {
          layout_shift_from_row = static_cast<int>(edit->start_point.row);
        }
      }
      highlights = highlights_after_incremental_parse(old_tree_for_highlights, tree, root,
                                                      job.source, previous_highlights,
                                                      layout_shift_from_row);
    } else {
      highlights = highlights_for_document(root, job.source);
    }
    if (!more_edits_pending) {
      symbols = extract_symbols_from_tree(root, job.source, job.path);
      scopes = scope_symbols_from_tree(root, job.source, job.path);
    }
  }
  if (old_tree_for_highlights != nullptr) {
    ts_tree_delete(old_tree_for_highlights);
  }

  ReadyCallback callback;
  bool schedule_followup = false;
  PrepareJob followup;
  bool committed = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(job.path);
    if (it == cache_.end()) {
      if (tree != nullptr) {
        ts_tree_delete(tree);
      }
      return;
    }
    DocumentEntry* entry = it->second.get();
    entry->prepare_inflight = false;
    if (entry->source != job.source) {
      followup.path = job.path;
      followup.source = entry->source;
      followup.previous_source = job.source;
      followup.previous_tree = tree;
      tree = nullptr;
      schedule_followup = true;
    } else {
      if (entry->tree != nullptr) {
        ts_tree_delete(entry->tree);
      }
      entry->tree = tree;
      entry->line_highlights = std::move(highlights);
      if (!more_edits_pending) {
        entry->symbols = std::move(symbols);
        entry->scope_symbols = std::move(scopes);
        entry->symbols_ready = entry->parse_ready;
      }
      entry->parse_ready = tree != nullptr;
      entry->highlights_ready = entry->parse_ready;
      entry->revision = next_revision_++;
      committed = true;
    }
  }

  if (schedule_followup) {
    bool queued = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto it = cache_.find(followup.path);
      if (it != cache_.end() && !it->second->prepare_inflight) {
        it->second->prepare_inflight = true;
        queued = true;
      }
    }
    if (queued) {
      std::lock_guard<std::mutex> worker_lock(worker_mutex_);
      pending_jobs_.erase(std::remove_if(pending_jobs_.begin(), pending_jobs_.end(),
                                         [&](const PrepareJob& pending) {
                                           if (pending.path == followup.path &&
                                               pending.previous_tree != nullptr) {
                                             ts_tree_delete(pending.previous_tree);
                                           }
                                           return pending.path == followup.path;
                                         }),
                           pending_jobs_.end());
      pending_jobs_.push_back(std::move(followup));
      worker_cv_.notify_one();
    } else {
      if (followup.previous_tree != nullptr) {
        ts_tree_delete(followup.previous_tree);
      }
      request_prepare(followup.path, followup.source);
    }
  }

  if (!committed) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback = ready_callback_;
  }
  if (callback) {
    callback(job.path);
  }
}

TSPoint make_ts_point(int line_0, int col) {
  TSPoint point;
  point.row = static_cast<uint32_t>(std::max(0, line_0));
  point.column = static_cast<uint32_t>(std::max(0, col));
  return point;
}

bool ts_point_in_node(TSNode node, TSPoint point) {
  if (ts_node_is_null(node)) {
    return false;
  }
  const TSPoint start = ts_node_start_point(node);
  const TSPoint end = ts_node_end_point(node);
  if (point.row < start.row || point.row > end.row) {
    return false;
  }
  if (point.row == start.row && point.column < start.column) {
    return false;
  }
  if (point.row == end.row && point.column > end.column) {
    return false;
  }
  return true;
}

}  // namespace tgdb
