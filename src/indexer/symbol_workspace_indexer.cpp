#include "indexer/symbol_workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>
#include <unordered_map>

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
  // Avoid racing the full scan (open tabs used to look like "the whole map").
  if (running_.load()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->partial = snapshot_->partial;
    updated->symbols.reserve(snapshot_->symbols.size());
    for (const auto& sym : snapshot_->symbols) {
      if (sym.file != relative_file) {
        updated->symbols.push_back(sym);
      }
    }
    for (const auto& ref : snapshot_->refs) {
      if (ref.file != relative_file) {
        updated->refs.push_back(ref);
      }
    }
  }

  const auto piece = index_relative_file_tags(workspace_root, relative_file);
  updated->symbols.insert(updated->symbols.end(), piece.symbols.begin(), piece.symbols.end());
  updated->refs.insert(updated->refs.end(), piece.refs.begin(), piece.refs.end());

  std::sort(updated->symbols.begin(), updated->symbols.end(),
            [](const IndexedSymbol& a, const IndexedSymbol& b) {
              if (a.file != b.file) {
                return a.file < b.file;
              }
              return a.name < b.name;
            });

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void SymbolWorkspaceIndexer::remove_file(const std::string& workspace_root,
                                         const std::string& relative_file) {
  if (relative_file.empty()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->partial = snapshot_->partial;
    for (const auto& sym : snapshot_->symbols) {
      if (sym.file != relative_file) {
        updated->symbols.push_back(sym);
      }
    }
    for (const auto& ref : snapshot_->refs) {
      if (ref.file != relative_file) {
        updated->refs.push_back(ref);
      }
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void SymbolWorkspaceIndexer::remove_path_prefix(const std::string& workspace_root,
                                                const std::string& prefix) {
  if (prefix.empty()) {
    return;
  }

  auto updated = std::make_shared<SymbolIndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->partial = snapshot_->partial;
    for (const auto& sym : snapshot_->symbols) {
      if (sym.file.rfind(prefix, 0) != 0) {
        updated->symbols.push_back(sym);
      }
    }
    for (const auto& ref : snapshot_->refs) {
      if (ref.file.rfind(prefix, 0) != 0) {
        updated->refs.push_back(ref);
      }
    }
  }

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void SymbolWorkspaceIndexer::stop() {
  stop_requested_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
  running_ = false;
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
}

}  // namespace tuide
