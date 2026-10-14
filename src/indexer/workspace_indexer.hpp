#pragma once

#include <atomic>
#include <condition_variable>
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
  // Alta/baja pedida por la UI (guardar, renombrar). El hilo de aplicación la
  // procesa antes que el lote de inotify.
  bool priority = false;
};

// Publicado por el hilo de aplicación cuando el snapshot ya refleja el lote.
// La UI solo repinta; no recorre directorios ni reindexa símbolos.
struct IndexApplyUpdate {
  bool wake_ui = false;
  // true solo al terminar el escaneo completo (rg), no en cada ráfaga de inotify.
  bool scan_finished = false;
  std::vector<std::string> removed_prefixes;
  std::vector<std::string> reindex_paths;
};

// Quiet period before waking UI for FS storms (make clean, bulk deletes).
constexpr int kIndexerFsChangeDebounceMs = 300;
// Flush even if events keep arriving (avoids never refreshing under sustained churn).
constexpr int kIndexerFsChangeMaxDebounceMs = 1000;

// Path equals prefix, or is a descendant (prefix + '/').
bool index_path_matches_prefix(const std::string& path, const std::string& prefix);

// Drop dominated RemovePrefix; dedupe consecutive Upserts (last absolute wins).
// Preserve order relative to IndexDirectory and Upsert↔Remove boundaries.
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
  // One snapshot copy + one derived-fields rebuild for a create/modify storm.
  void upsert_files(const std::string& workspace_root,
                    const std::vector<FileIndexChange>& upserts);
  void remove_file(const std::string& workspace_root, const std::string& relative_file);
  // Recorre el directorio y publica el snapshot. false si se aborta o no hay índice.
  bool index_directory(const std::string& workspace_root, const std::string& relative_dir,
                       const std::string& absolute_dir);
  void remove_path_prefix(const std::string& workspace_root, const std::string& prefix);
  // One snapshot copy + one derived-fields rebuild for an entire delete storm.
  void remove_path_prefixes(const std::string& workspace_root,
                            const std::vector<std::string>& prefixes);
  bool refresh(const std::string& workspace_root);
  // Actualiza el catálogo de Ctrl+P sin reindexar. No toca el explorador.
  void set_file_picker_exclude_dirs(std::vector<std::string> dirs);
  void stop();
  // Se llama desde el hilo de aplicación (o al acabar el escaneo), con el
  // snapshot ya publicado. No hacer trabajo de disco aquí.
  void set_change_notify(std::function<void(const IndexApplyUpdate&)> callback);
  // Optional: return true if absolute_path is shown in an editor (active tab).
  // Called from the inotify thread on content-only modifies.
  void set_modify_wake_predicate(std::function<bool(const std::string& absolute_path)> pred);
  std::shared_ptr<const IndexSnapshot> snapshot() const;
  bool scanning() const;
  bool has_pending_changes() const;
  std::vector<FileIndexChange> drain_changes();

 private:
  void worker_main(std::string workspace_root, IndexFilterOptions filter_options);
  void apply_loop();
  void enqueue_change(FileIndexChange change);
  void signal_apply();
  void apply_changes_locked(std::vector<FileIndexChange> changes, IndexApplyUpdate* update);
  void notify_apply(const IndexApplyUpdate& update);

  mutable std::mutex mutex_;
  // Serializa publicaciones del snapshot (escaneo, ráfaga, refresh).
  std::mutex mutation_mutex_;
  std::shared_ptr<const IndexSnapshot> snapshot_;
  mutable std::mutex changes_mutex_;
  std::vector<FileIndexChange> pending_changes_;
  bool pending_batch_wake_ = false;
  std::function<void(const IndexApplyUpdate&)> change_notify_;
  std::function<bool(const std::string& absolute_path)> modify_wake_predicate_;
  mutable std::mutex modify_wake_mutex_;
  std::vector<std::string> file_picker_exclude_dirs_;
  std::thread worker_;
  std::thread apply_thread_;
  std::mutex apply_wait_mutex_;
  std::condition_variable apply_cv_;
  bool apply_signaled_ = false;
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
