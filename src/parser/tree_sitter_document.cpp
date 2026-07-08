#include "parser/tree_sitter_document.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>

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

bool apply_sync_source_edit(DocumentEntry* entry, const std::string& canonical) {
  if (entry == nullptr || entry->tree == nullptr || entry->source == canonical) {
    return false;
  }
  const std::optional<TSInputEdit> edit = single_edit_between(entry->source, canonical);
  if (!edit.has_value()) {
    return false;
  }
  ts_tree_edit(entry->tree, &*edit);
  entry->source = canonical;
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
  if (source.empty()) {
    return source;
  }
  std::vector<std::string> lines;
  std::istringstream input(source);
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  if (lines.empty()) {
    lines.push_back("");
  }
  return join_editor_lines(lines);
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

void TreeSitterDocumentCache::request_prepare(const std::string& path,
                                              const std::string& source) {
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
      if (!apply_sync_source_edit(entry.get(), canonical)) {
        debounced.previous_source = entry->source;
        debounced.previous_tree = entry->tree;
        entry->tree = nullptr;
        entry->source = canonical;
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
      highlights = highlights_after_incremental_parse(old_tree_for_highlights, tree, root,
                                                    job.source, previous_highlights);
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
