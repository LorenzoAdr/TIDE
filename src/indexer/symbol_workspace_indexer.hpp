#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"

namespace tuide {

struct IndexedSymbol {
  std::string display_name;  // may include outline prefix
  std::string name;          // bare identifier (preferred for ranking)
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  std::string file;
  std::string signature;  // trimmed source line of the definition
};

// Identifier reference aggregated per (from_file, name) for PageRank edges.
struct IndexedRef {
  std::string file;
  std::string name;
  int count = 1;
};

struct SymbolIndexSnapshot {
  std::string workspace_root;
  std::vector<IndexedSymbol> symbols;
  std::vector<IndexedRef> refs;
  bool partial = false;  // true while full scan still running (incremental publish)
};

class SymbolWorkspaceIndexer {
 public:
  SymbolWorkspaceIndexer();
  ~SymbolWorkspaceIndexer();

  using ProgressCallback =
      std::function<void(bool scanning, std::size_t done, std::size_t total)>;

  void start_scan(const std::string& workspace_root,
                  const std::shared_ptr<ISymbolProvider>& provider,
                  WorkspaceIndexer* file_indexer);
  void reindex_file(const std::string& workspace_root,
                    const std::string& relative_file,
                    const std::string& absolute_path);
  void remove_file(const std::string& workspace_root, const std::string& relative_file);
  void remove_path_prefix(const std::string& workspace_root, const std::string& prefix);
  void remove_path_prefixes(const std::string& workspace_root,
                            const std::vector<std::string>& prefixes);
  void stop();
  std::shared_ptr<const SymbolIndexSnapshot> snapshot() const;
  bool scanning() const;
  // Progress while scanning (0/0 when idle). current_file may be empty.
  void progress(std::size_t* done, std::size_t* total, std::string* current_file = nullptr) const;
  // Optional: called from the worker thread as scan progress changes (and on finish).
  void set_progress_callback(ProgressCallback callback);

 private:
  void worker_main(std::string workspace_root,
                   std::shared_ptr<ISymbolProvider> provider,
                   WorkspaceIndexer* file_indexer);
  void publish_snapshot_locked(std::shared_ptr<SymbolIndexSnapshot> snap);
  void notify_progress(bool scanning, std::size_t done, std::size_t total);

  mutable std::mutex mutex_;
  std::shared_ptr<const SymbolIndexSnapshot> snapshot_;
  std::shared_ptr<ISymbolProvider> provider_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<std::size_t> progress_done_{0};
  std::atomic<std::size_t> progress_total_{0};
  std::string progress_file_;
  mutable std::mutex callback_mutex_;
  ProgressCallback progress_callback_;
};

}  // namespace tuide
