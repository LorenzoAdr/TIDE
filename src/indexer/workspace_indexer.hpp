#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "indexer/index_rules.hpp"

namespace tuide {

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
  // true: create/delete/rename/dir — FileTree / picker listing may change.
  // false: content-only modify — update index on next tick, do not wake UI.
  bool wake_ui = false;
};

// Quiet period before waking UI for FS storms (make clean, bulk deletes).
constexpr int kIndexerFsChangeDebounceMs = 300;
// Flush even if events keep arriving (avoids never refreshing under sustained churn).
constexpr int kIndexerFsChangeMaxDebounceMs = 1000;

// Path equals prefix, or is a descendant (prefix + '/').
bool index_path_matches_prefix(const std::string& path, const std::string& prefix);

// Drop RemovePrefix entries dominated by a parent remove; preserve order vs Upsert/IndexDirectory.
std::vector<FileIndexChange> coalesce_file_index_changes(std::vector<FileIndexChange> changes);

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
  // One snapshot copy + one derived-fields rebuild for an entire delete storm.
  void remove_path_prefixes(const std::string& workspace_root,
                            const std::vector<std::string>& prefixes);
  bool refresh(const std::string& workspace_root);
  void stop();
  // Callback receives whether any queued change needs a UI wake (tree listing
  // or a modify of a file currently visible in the editor).
  void set_change_notify(std::function<void(bool wake_ui)> callback);
  // Optional: return true if absolute_path is shown in an editor (active tab).
  // Called from the inotify thread on content-only modifies.
  void set_modify_wake_predicate(std::function<bool(const std::string& absolute_path)> pred);
  std::shared_ptr<const IndexSnapshot> snapshot() const;
  bool scanning() const;
  bool has_pending_changes() const;
  std::vector<FileIndexChange> drain_changes();

 private:
  void worker_main(std::string workspace_root, IndexFilterOptions filter_options);

  mutable std::mutex mutex_;
  std::shared_ptr<const IndexSnapshot> snapshot_;
  mutable std::mutex changes_mutex_;
  std::vector<FileIndexChange> pending_changes_;
  std::function<void(bool wake_ui)> change_notify_;
  std::function<bool(const std::string& absolute_path)> modify_wake_predicate_;
  mutable std::mutex modify_wake_mutex_;
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

}  // namespace tuide
