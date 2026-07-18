#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/workspace_indexer.hpp"
#include "symbols/symbol_provider.hpp"

namespace tuide {

struct IndexedSymbol {
  std::string display_name;
  SymbolKind kind = SymbolKind::kFunction;
  int line = 0;
  std::string file;
};

struct SymbolIndexSnapshot {
  std::string workspace_root;
  std::vector<IndexedSymbol> symbols;
};

class SymbolWorkspaceIndexer {
 public:
  SymbolWorkspaceIndexer();
  ~SymbolWorkspaceIndexer();

  void start_scan(const std::string& workspace_root,
                  const std::shared_ptr<ISymbolProvider>& provider,
                  WorkspaceIndexer* file_indexer);
  void reindex_file(const std::string& workspace_root,
                    const std::string& relative_file,
                    const std::string& absolute_path);
  void remove_file(const std::string& workspace_root, const std::string& relative_file);
  void remove_path_prefix(const std::string& workspace_root, const std::string& prefix);
  void stop();
  std::shared_ptr<const SymbolIndexSnapshot> snapshot() const;
  bool scanning() const;

 private:
  void worker_main(std::string workspace_root,
                   std::shared_ptr<ISymbolProvider> provider,
                   WorkspaceIndexer* file_indexer);

  mutable std::mutex mutex_;
  std::shared_ptr<const SymbolIndexSnapshot> snapshot_;
  std::shared_ptr<ISymbolProvider> provider_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
};

}  // namespace tuide
