#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/trie.hpp"

namespace tgdb {

struct IndexSnapshot {
  std::string workspace_root;
  std::vector<std::string> files;
  Trie trie;
};

class WorkspaceIndexer {
 public:
  WorkspaceIndexer();
  ~WorkspaceIndexer();

  void start_scan(const std::string& workspace_root);
  void stop();
  std::shared_ptr<const IndexSnapshot> snapshot() const;
  bool scanning() const;

 private:
  void worker_main(std::string workspace_root);

  mutable std::mutex mutex_;
  std::shared_ptr<const IndexSnapshot> snapshot_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_requested_{false};
};

std::vector<std::string> scan_workspace_files(const std::string& workspace_root);

}  // namespace tgdb
