#include "indexer/symbol_workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "indexer/index_rules.hpp"
#include "parser/tree_sitter_tags.hpp"
#include "symbols/symbol_utils.hpp"
#include "symbols/tree_sitter_symbol_provider.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

struct FileIndexPiece {
  std::vector<IndexedSymbol> symbols;
  std::vector<IndexedRef> refs;
};

FileIndexPiece index_relative_file_tags(const std::string& workspace_root,
                                        const std::string& relative_file) {
  FileIndexPiece piece;
  if (relative_file.empty()) {
    return piece;
  }
  const auto tags = extract_repo_map_tags_for_file(workspace_root, relative_file);
  std::unordered_map<std::string, int> ref_counts;
  for (const auto& tag : tags) {
    if (tag.tag_kind == RepoMapTagKind::Def) {
      IndexedSymbol entry;
      entry.display_name = tag.name;
      entry.name = tag.name;
      entry.kind = tag.symbol_kind;
      entry.line = tag.line;
      // Always store workspace-relative paths (never absolute) for git/PageRank matching.
      entry.file = relative_file;
      entry.signature = tag.signature;
      piece.symbols.push_back(std::move(entry));
    } else {
      ++ref_counts[tag.name];
    }
  }
  for (auto& [name, count] : ref_counts) {
    IndexedRef r;
    r.file = relative_file;
    r.name = name;
    r.count = count;
    piece.refs.push_back(std::move(r));
  }
  return piece;
}

FileIndexPiece index_relative_file_fallback(const std::shared_ptr<ISymbolProvider>& provider,
                                            const std::string& workspace_root,
                                            const std::string& relative_file) {
  FileIndexPiece piece;
  if (!provider || relative_file.empty()) {
    return piece;
  }
  const auto absolute = (fs::path(workspace_root) / relative_file).string();
  for (const auto& sym : provider->symbols_for_file(absolute)) {
    IndexedSymbol entry;
    entry.display_name = sym.name;
    entry.name = symbol_insert_name(sym.name);
    entry.kind = sym.kind;
    entry.line = sym.line;
    entry.file = relative_file;
    piece.symbols.push_back(std::move(entry));
  }
  return piece;
}

bool file_matches_any_prefix(const std::string& file, const std::vector<std::string>& prefixes) {
  for (const std::string& prefix : prefixes) {
    if (index_path_matches_prefix(file, prefix)) {
      return true;
    }
  }
  return false;
}

void erase_symbol_prefixes(SymbolIndexSnapshot* snap, const std::vector<std::string>& prefixes) {
  if (snap == nullptr || prefixes.empty()) {
    return;
  }
  const auto drop = [&](const std::string& file) {
    return file_matches_any_prefix(file, prefixes);
  };
  snap->symbols.erase(std::remove_if(snap->symbols.begin(), snap->symbols.end(),
                                      [&](const IndexedSymbol& symbol) { return drop(symbol.file); }),
                      snap->symbols.end());
  snap->refs.erase(std::remove_if(snap->refs.begin(), snap->refs.end(),
                                  [&](const IndexedRef& ref) { return drop(ref.file); }),
                   snap->refs.end());
}

void erase_exact_files(SymbolIndexSnapshot* snap, const std::unordered_set<std::string>& files) {
  if (snap == nullptr || files.empty()) {
    return;
  }
  snap->symbols.erase(std::remove_if(snap->symbols.begin(), snap->symbols.end(),
                                      [&](const IndexedSymbol& symbol) {
                                        return files.count(symbol.file) > 0;
                                      }),
                      snap->symbols.end());
  snap->refs.erase(std::remove_if(snap->refs.begin(), snap->refs.end(),
                                  [&](const IndexedRef& ref) { return files.count(ref.file) > 0; }),
                   snap->refs.end());
}

bool symbol_less(const IndexedSymbol& a, const IndexedSymbol& b) {
  if (a.file != b.file) {
    return a.file < b.file;
  }
  return a.name < b.name;
}

}  // namespace

SymbolWorkspaceIndexer::SymbolWorkspaceIndexer() {
  snapshot_ = std::make_shared<SymbolIndexSnapshot>();
}

SymbolWorkspaceIndexer::~SymbolWorkspaceIndexer() {
  stop();
}

void SymbolWorkspaceIndexer::publish_snapshot_locked(std::shared_ptr<SymbolIndexSnapshot> snap) {
  snapshot_ = std::move(snap);
}

void SymbolWorkspaceIndexer::start_scan(const std::string& workspace_root,
                                        const std::shared_ptr<ISymbolProvider>& provider,
                                        WorkspaceIndexer* file_indexer) {
  stop();
  provider_ = provider;
  stop_requested_ = false;
  progress_done_ = 0;
  progress_total_ = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_file_.clear();
    // Keep previous snapshot visible until the worker publishes the first incremental batch.
    if (!snapshot_) {
      auto snap = std::make_shared<SymbolIndexSnapshot>();
      snap->workspace_root = workspace_root;
      snap->partial = true;
      snapshot_ = snap;
    } else if (snapshot_->workspace_root != workspace_root) {
      auto snap = std::make_shared<SymbolIndexSnapshot>();
      snap->workspace_root = workspace_root;
      snap->partial = true;
      snapshot_ = snap;
    }
  }
  running_ = true;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    worker_alive_ = true;
    edit_queue_.clear();
  }
  worker_ = std::thread([this, workspace_root, provider, file_indexer] {
    set_current_thread_name("idx-syms");
    worker_main(workspace_root, provider, file_indexer);
  });
}

void SymbolWorkspaceIndexer::reindex_file(const std::string& workspace_root,
                                          const std::string& relative_file,
                                          const std::string& absolute_path) {
  (void)absolute_path;
  if (!is_indexed_source_path(relative_file) || relative_file.empty()) {
    return;
  }
  EditBatch batch;
  batch.reindex_paths.push_back(relative_file);
  batch.priority = true;
  if (!enqueue_batch(std::move(batch))) {
    reindex_paths_now(workspace_root, provider_, {relative_file});
  }
}

void SymbolWorkspaceIndexer::enqueue_fs_changes(std::vector<std::string> removed_prefixes,
                                                std::vector<std::string> reindex_paths) {
  if (removed_prefixes.empty() && reindex_paths.empty()) {
    return;
  }
  EditBatch batch;
  batch.remove_prefixes = std::move(removed_prefixes);
  batch.reindex_paths = std::move(reindex_paths);
  enqueue_batch(std::move(batch));
}

void SymbolWorkspaceIndexer::remove_file(const std::string& workspace_root,
                                         const std::string& relative_file) {
  if (relative_file.empty()) {
    return;
  }
  remove_path_prefixes(workspace_root, std::vector<std::string>{relative_file});
}

void SymbolWorkspaceIndexer::remove_path_prefix(const std::string& workspace_root,
                                                const std::string& prefix) {
  if (prefix.empty()) {
    return;
  }
  remove_path_prefixes(workspace_root, std::vector<std::string>{prefix});
}

void SymbolWorkspaceIndexer::remove_path_prefixes(const std::string& workspace_root,
                                                  const std::vector<std::string>& prefixes) {
  if (prefixes.empty()) {
    return;
  }
  EditBatch batch;
  batch.remove_prefixes = prefixes;
  batch.priority = true;
  if (!enqueue_batch(std::move(batch))) {
    remove_prefixes_now(workspace_root, prefixes);
  }
}

bool SymbolWorkspaceIndexer::enqueue_batch(EditBatch batch) {
  if (batch.remove_prefixes.empty() && batch.reindex_paths.empty()) {
    return true;
  }
  std::lock_guard<std::mutex> lock(queue_mutex_);
  if (!worker_alive_.load()) {
    return false;
  }
  if (batch.priority) {
    edit_queue_.push_front(std::move(batch));
  } else {
    edit_queue_.push_back(std::move(batch));
  }
  queue_cv_.notify_one();
  return true;
}

std::vector<SymbolWorkspaceIndexer::EditBatch> SymbolWorkspaceIndexer::steal_priority_batches() {
  std::vector<EditBatch> out;
  std::lock_guard<std::mutex> lock(queue_mutex_);
  for (auto it = edit_queue_.begin(); it != edit_queue_.end();) {
    if (!it->priority) {
      ++it;
      continue;
    }
    out.push_back(std::move(*it));
    it = edit_queue_.erase(it);
  }
  return out;
}

void SymbolWorkspaceIndexer::remove_prefixes_now(const std::string& workspace_root,
                                                 const std::vector<std::string>& prefixes) {
  if (prefixes.empty()) {
    return;
  }
  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    *updated = *snapshot_;
  }
  erase_symbol_prefixes(updated.get(), prefixes);
  std::lock_guard<std::mutex> lock(mutex_);
  if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
    return;
  }
  snapshot_ = std::move(updated);
}

void SymbolWorkspaceIndexer::reindex_paths_now(const std::string& workspace_root,
                                               const std::shared_ptr<ISymbolProvider>& provider,
                                               std::vector<std::string> relative_files) {
  (void)provider;
  std::vector<std::string> sources;
  std::unordered_set<std::string> seen;
  sources.reserve(relative_files.size());
  for (std::string& relative : relative_files) {
    if (!is_indexed_source_path(relative) || !seen.insert(relative).second) {
      continue;
    }
    sources.push_back(std::move(relative));
  }
  if (sources.empty()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    *updated = *snapshot_;
  }
  erase_exact_files(updated.get(), seen);

  std::vector<std::string> blocked;
  std::unordered_set<std::string> done;
  auto last_publish = std::chrono::steady_clock::now();
  bool dirty = true;

  auto publish = [&](bool force) {
    if (!dirty && !force) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (!force && now - last_publish < std::chrono::milliseconds(200)) {
      return;
    }
    auto copy = std::make_shared<SymbolIndexSnapshot>(*updated);
    copy->partial = false;
    std::sort(copy->symbols.begin(), copy->symbols.end(), symbol_less);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
        return;
      }
      snapshot_ = std::move(copy);
    }
    last_publish = now;
    dirty = false;
  };

  auto absorb_priority = [&]() {
    for (EditBatch& batch : steal_priority_batches()) {
      if (!batch.remove_prefixes.empty()) {
        erase_symbol_prefixes(updated.get(), batch.remove_prefixes);
        blocked.insert(blocked.end(), batch.remove_prefixes.begin(), batch.remove_prefixes.end());
        dirty = true;
      }
      for (const std::string& path : batch.reindex_paths) {
        if (!is_indexed_source_path(path) || file_matches_any_prefix(path, blocked)) {
          continue;
        }
        erase_exact_files(updated.get(), {path});
        const FileIndexPiece piece = index_relative_file_tags(workspace_root, path);
        updated->symbols.insert(updated->symbols.end(), piece.symbols.begin(), piece.symbols.end());
        updated->refs.insert(updated->refs.end(), piece.refs.begin(), piece.refs.end());
        done.insert(path);
        dirty = true;
      }
    }
  };

  for (const std::string& relative : sources) {
    if (worker_alive_.load() && stop_requested_.load()) {
      break;
    }
    absorb_priority();
    if (done.count(relative) > 0 || file_matches_any_prefix(relative, blocked)) {
      continue;
    }
    const FileIndexPiece piece = index_relative_file_tags(workspace_root, relative);
    updated->symbols.insert(updated->symbols.end(), piece.symbols.begin(), piece.symbols.end());
    updated->refs.insert(updated->refs.end(), piece.refs.begin(), piece.refs.end());
    done.insert(relative);
    dirty = true;
    publish(false);
  }
  publish(true);
}

void SymbolWorkspaceIndexer::idle_loop(std::string workspace_root,
                                       std::shared_ptr<ISymbolProvider> provider) {
  while (!stop_requested_.load()) {
    std::vector<EditBatch> ready;
    {
      std::unique_lock<std::mutex> lock(queue_mutex_);
      queue_cv_.wait(lock, [this] {
        return stop_requested_.load() || !edit_queue_.empty();
      });
      if (stop_requested_.load() && edit_queue_.empty()) {
        return;
      }
      while (!edit_queue_.empty()) {
        ready.push_back(std::move(edit_queue_.front()));
        edit_queue_.pop_front();
      }
    }
    std::stable_partition(ready.begin(), ready.end(),
                          [](const EditBatch& batch) { return batch.priority; });
    for (EditBatch& batch : ready) {
      if (stop_requested_.load()) {
        return;
      }
      if (!batch.remove_prefixes.empty()) {
        remove_prefixes_now(workspace_root, batch.remove_prefixes);
      }
      if (!batch.reindex_paths.empty()) {
        reindex_paths_now(workspace_root, provider, std::move(batch.reindex_paths));
      }
    }
  }
}

void SymbolWorkspaceIndexer::stop() {
  stop_requested_ = true;
  queue_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  running_ = false;
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    worker_alive_ = false;
    edit_queue_.clear();
  }
  notify_progress(false, progress_done_.load(), progress_total_.load());
}

std::shared_ptr<const SymbolIndexSnapshot> SymbolWorkspaceIndexer::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool SymbolWorkspaceIndexer::scanning() const {
  return running_.load();
}

void SymbolWorkspaceIndexer::progress(std::size_t* done, std::size_t* total,
                                      std::string* current_file) const {
  if (done != nullptr) {
    *done = progress_done_.load();
  }
  if (total != nullptr) {
    *total = progress_total_.load();
  }
  if (current_file != nullptr) {
    std::lock_guard<std::mutex> lock(mutex_);
    *current_file = progress_file_;
  }
}

void SymbolWorkspaceIndexer::set_progress_callback(ProgressCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  progress_callback_ = std::move(callback);
}

void SymbolWorkspaceIndexer::notify_progress(bool scanning, std::size_t done, std::size_t total) {
  ProgressCallback cb;
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    cb = progress_callback_;
  }
  if (cb) {
    cb(scanning, done, total);
  }
}

void SymbolWorkspaceIndexer::worker_main(std::string workspace_root,
                                         std::shared_ptr<ISymbolProvider> provider,
                                         WorkspaceIndexer* file_indexer) {
  TUIDE_MON_SCOPE("idx", "symbol_workspace_indexer.scan");
  progress_done_ = 0;
  progress_total_ = 0;
  notify_progress(true, 0, 0);

  std::vector<std::string> files;
  if (file_indexer != nullptr) {
    // Wait for the *full* workspace file list. Taking the early skeleton snapshot
    // (root + open-file folder) used to freeze the AI map at ~dozens of files.
    auto last_wait_notify = std::chrono::steady_clock::now();
    for (;;) {
      if (stop_requested_) {
        running_ = false;
        notify_progress(false, progress_done_.load(), progress_total_.load());
        return;
      }
      if (!file_indexer->scanning()) {
        const auto file_snap = file_indexer->snapshot();
        if (file_snap && file_snap->workspace_root == workspace_root) {
          files = file_snap->files;
        }
        break;
      }
      const auto now = std::chrono::steady_clock::now();
      if (now - last_wait_notify >= std::chrono::milliseconds(200)) {
        notify_progress(true, 0, 0);
        last_wait_notify = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  std::vector<std::string> source_files;
  source_files.reserve(files.size());
  for (const auto& rel : files) {
    if (is_indexed_source_path(rel)) {
      source_files.push_back(rel);
    }
  }
  progress_total_ = source_files.size();
  progress_done_ = 0;
  notify_progress(true, 0, source_files.size());
  TUIDE_MON("idx", "symbol_workspace_indexer.sources=" + std::to_string(source_files.size()));

  auto snap = std::make_shared<SymbolIndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->partial = true;

  constexpr std::size_t kPublishEvery = 20;
  for (std::size_t i = 0; i < source_files.size(); ++i) {
    if (stop_requested_) {
      running_ = false;
      notify_progress(false, progress_done_.load(), progress_total_.load());
      return;
    }
    const auto& rel = source_files[i];
    {
      std::lock_guard<std::mutex> lock(mutex_);
      progress_file_ = rel;
    }
    FileIndexPiece piece = index_relative_file_tags(workspace_root, rel);
    if (piece.symbols.empty() && provider) {
      piece = index_relative_file_fallback(provider, workspace_root, rel);
    }
    snap->symbols.insert(snap->symbols.end(), piece.symbols.begin(), piece.symbols.end());
    snap->refs.insert(snap->refs.end(), piece.refs.begin(), piece.refs.end());
    progress_done_ = i + 1;

    // Progress strip updates more often than snapshot publishes.
    if ((i + 1) % 5 == 0 || i + 1 == source_files.size()) {
      notify_progress(true, i + 1, source_files.size());
    }

    if ((i + 1) % kPublishEvery == 0 || i + 1 == source_files.size()) {
      auto published = std::make_shared<SymbolIndexSnapshot>(*snap);
      published->partial = (i + 1) < source_files.size();
      std::sort(published->symbols.begin(), published->symbols.end(),
                [](const IndexedSymbol& a, const IndexedSymbol& b) {
                  if (a.file != b.file) {
                    return a.file < b.file;
                  }
                  return a.name < b.name;
                });
      {
        std::lock_guard<std::mutex> lock(mutex_);
        publish_snapshot_locked(std::move(published));
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_file_.clear();
    if (snapshot_) {
      // Ensure final flag.
      auto final_snap = std::make_shared<SymbolIndexSnapshot>(*snapshot_);
      final_snap->partial = false;
      snapshot_ = final_snap;
    }
  }
  running_ = false;
  notify_progress(false, progress_done_.load(), progress_total_.load());
  if (stop_requested_.load()) {
    return;
  }
  // Los ficheros que aparecieron durante el escaneo quedan en la cola.
  idle_loop(std::move(workspace_root), std::move(provider));
}

}  // namespace tuide
