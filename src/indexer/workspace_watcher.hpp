#pragma once

#include <memory>
#include <string>
#include <vector>

#include "indexer/index_rules.hpp"

namespace tgdb {

enum class FileIndexChangeKind { Upsert, Remove };

struct FileIndexChange {
  FileIndexChangeKind kind = FileIndexChangeKind::Upsert;
  std::string relative_path;
  std::string absolute_path;
};

class WorkspaceWatcher {
 public:
  WorkspaceWatcher();
  ~WorkspaceWatcher();

  void start(const std::string& workspace_root,
             const IndexFilterOptions& filter_options = {});
  void stop();
  std::vector<FileIndexChange> drain_changes();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tgdb
