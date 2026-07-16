#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/index_rules.hpp"

namespace tgdb {

struct FilePickerCatalogEntry {
  std::string path;
  std::string display_label;
  std::string filename;
  std::string filename_lower;
  std::string dir_label;
};

struct IndexSnapshot {
  std::string workspace_root;
  std::vector<std::string> files;
  std::vector<std::string> files_lower;
  std::shared_ptr<const std::vector<FilePickerCatalogEntry>> file_picker_catalog;
  // Carpetas de primer nivel del esqueleto inicial; vacío tras el escaneo completo.
  std::vector<std::string> skeleton_folders;
  // Directorios del workspace (incluye vacíos); se mantiene tras el escaneo completo.
  std::vector<std::string> folders;
  IndexFilterOptions filter_options;
};

enum class FileIndexChangeKind { Upsert, Remove, IndexDirectory, RemovePrefix };

struct FileIndexChange {
  FileIndexChangeKind kind = FileIndexChangeKind::Upsert;
  std::string relative_path;
  std::string absolute_path;
};

class WorkspaceIndexer {
 public:
  WorkspaceIndexer();
  ~WorkspaceIndexer();

  void start_scan(const std::string& workspace_root,
                  const IndexFilterOptions& filter_options = {},
                  const std::string& anchor_path = {},
                  const std::string& open_file_path = {});
  void upsert_file(const std::string& workspace_root, const std::string& relative_file,
                   const std::string& absolute_path);
  void remove_file(const std::string& workspace_root, const std::string& relative_file);
  void index_directory(const std::string& workspace_root, const std::string& relative_dir,
                       const std::string& absolute_dir);
  void remove_path_prefix(const std::string& workspace_root, const std::string& prefix);
  bool refresh(const std::string& workspace_root);
  void stop();
  std::shared_ptr<const IndexSnapshot> snapshot() const;
  bool scanning() const;
  std::vector<FileIndexChange> drain_changes();

 private:
  void worker_main(std::string workspace_root, IndexFilterOptions filter_options);

  mutable std::mutex mutex_;
  std::shared_ptr<const IndexSnapshot> snapshot_;
  std::mutex changes_mutex_;
  std::vector<FileIndexChange> pending_changes_;
  std::thread worker_;
  std::atomic<bool> scanning_{false};
  std::atomic<bool> stop_requested_{false};
  std::atomic<pid_t> rg_child_pid_{-1};
#if defined(__linux__)
  int inotify_fd_ = -1;
#endif
};

std::vector<std::string> scan_workspace_files(const std::string& workspace_root,
                                                const IndexFilterOptions& filter_options = {});

void rebuild_index_files_lower(IndexSnapshot* snapshot);
void rebuild_index_file_picker_catalog(IndexSnapshot* snapshot);
void rebuild_index_derived_fields(IndexSnapshot* snapshot);

}  // namespace tgdb
