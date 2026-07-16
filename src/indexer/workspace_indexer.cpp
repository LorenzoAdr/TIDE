#include "indexer/workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "indexer/index_rules.hpp"
#include "indexer/workspace_indexer_rg.hpp"
#include "util/fuzzy_match.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

#if defined(__linux__)
#include <signal.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string relative_path_in_workspace(const fs::path& workspace_root,
                                       const fs::path& absolute_path) {
  std::error_code ec;
  const fs::path rel = fs::relative(absolute_path, workspace_root, ec);
  if (ec || rel.empty() || rel == ".") {
    return {};
  }
  return rel.generic_string();
}

void list_directory_skeleton(const fs::path& workspace_root, const fs::path& dir_relative,
                             const IndexFilterOptions& options, std::vector<std::string>* files,
                             std::vector<std::string>* folders) {
  const fs::path absolute = workspace_root / dir_relative;
  std::error_code ec;
  if (!fs::is_directory(absolute, ec)) {
    return;
  }

  for (const auto& entry : fs::directory_iterator(absolute, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (name.empty() || name == "." || name == "..") {
      continue;
    }

    const fs::path rel = dir_relative.empty() ? fs::path(name) : dir_relative / name;
    const std::string rel_str = rel.generic_string();

    if (entry.is_directory(ec)) {
      if (should_skip_dir_name(name, options)) {
        continue;
      }
      folders->push_back(rel_str);
    } else if (entry.is_regular_file(ec)) {
      if (should_list_workspace_path(rel_str, options)) {
        files->push_back(rel_str);
      }
    }
  }
}

void scan_workspace_skeleton(const std::string& workspace_root,
                             const IndexFilterOptions& filter_options,
                             const std::string& anchor_path,
                             const std::string& open_file_path, IndexSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }

  std::error_code ec;
  const fs::path root = fs::absolute(workspace_root, ec);
  if (!fs::is_directory(root, ec)) {
    return;
  }

  std::vector<std::string> scan_dirs;
  scan_dirs.push_back("");

  const std::string anchor_rel = relative_path_in_workspace(root, fs::path(anchor_path));
  if (!anchor_rel.empty()) {
    scan_dirs.push_back(anchor_rel);
  }

  if (!open_file_path.empty()) {
    const std::string file_rel = relative_path_in_workspace(root, fs::path(open_file_path));
    if (!file_rel.empty()) {
      const fs::path parent = fs::path(file_rel).parent_path();
      const std::string parent_rel = parent.empty() || parent == "." ? "" : parent.generic_string();
      if (std::find(scan_dirs.begin(), scan_dirs.end(), parent_rel) == scan_dirs.end()) {
        scan_dirs.push_back(parent_rel);
      }
    }
  }

  std::vector<std::string> files;
  std::vector<std::string> folders;
  for (const auto& dir : scan_dirs) {
    list_directory_skeleton(root, fs::path(dir), filter_options, &files, &folders);
  }

  std::sort(files.begin(), files.end());
  std::sort(folders.begin(), folders.end());
  folders.erase(std::unique(folders.begin(), folders.end()), folders.end());

  snapshot->files = std::move(files);
  snapshot->skeleton_folders = std::move(folders);
  rebuild_index_derived_fields(snapshot);
}

void scan_dir(const fs::path& root, const fs::path& current, const IndexFilterOptions& options,
              std::vector<std::string>* out) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      if (should_skip_dir_name(name, options)) {
        continue;
      }
      scan_dir(root, entry.path(), options, out);
    } else if (entry.is_regular_file(ec)) {
      std::error_code rel_ec;
      const auto rel = fs::relative(entry.path(), root, rel_ec);
      if (!rel_ec && should_list_workspace_path(rel.generic_string(), options)) {
        out->push_back(rel.generic_string());
      }
    }
  }
}

void collect_workspace_directories(const fs::path& root, const fs::path& current,
                                   const IndexFilterOptions& options,
                                   std::vector<std::string>* folders) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (should_skip_dir_name(name, options)) {
      continue;
    }
    const fs::path rel = fs::relative(entry.path(), root, ec);
    if (ec || rel.empty()) {
      continue;
    }
    folders->push_back(rel.generic_string());
    collect_workspace_directories(root, entry.path(), options, folders);
  }
}

bool path_matches_prefix(const std::string& path, const std::string& prefix) {
  if (prefix.empty()) {
    return false;
  }
  if (path == prefix) {
    return true;
  }
  if (path.size() <= prefix.size() || path[prefix.size()] != '/') {
    return false;
  }
  return path.rfind(prefix, 0) == 0;
}

void sort_unique_strings(std::vector<std::string>* values) {
  if (values == nullptr) {
    return;
  }
  std::sort(values->begin(), values->end());
  values->erase(std::unique(values->begin(), values->end()), values->end());
}

void insert_sorted_unique(std::vector<std::string>* values, const std::string& value) {
  if (values == nullptr || value.empty()) {
    return;
  }
  if (std::binary_search(values->begin(), values->end(), value)) {
    return;
  }
  const auto it = std::lower_bound(values->begin(), values->end(), value);
  values->insert(it, value);
}

#if defined(__linux__)

constexpr int kWatchMask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM |
                           IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

void add_directory_watch(int fd, const fs::path& dir,
                         std::unordered_map<int, fs::path>* watch_dirs) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return;
  }
  const int wd = inotify_add_watch(fd, dir.c_str(), kWatchMask);
  if (wd < 0) {
    return;
  }
  (*watch_dirs)[wd] = dir;
}

void scan_directories_for_watch(int fd, const fs::path& root, const fs::path& current,
                                const IndexFilterOptions& options,
                                std::unordered_map<int, fs::path>* watch_dirs) {
  add_directory_watch(fd, current, watch_dirs);
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (should_skip_dir_name(name, options)) {
      continue;
    }
    scan_directories_for_watch(fd, root, entry.path(), options, watch_dirs);
  }
}

void run_inotify_loop(const std::string& workspace_root, const IndexFilterOptions& filter_options,
                      int inotify_fd, std::atomic<bool>* stop_requested,
                      std::mutex* changes_mutex, std::vector<FileIndexChange>* pending_changes,
                      const std::function<void()>& on_changes) {
  std::unordered_map<int, fs::path> watch_dirs;
  const fs::path root(workspace_root);
  scan_directories_for_watch(inotify_fd, root, root, filter_options, &watch_dirs);

  auto push_change = [&](FileIndexChange change) {
    {
      std::lock_guard<std::mutex> lock(*changes_mutex);
      pending_changes->push_back(std::move(change));
    }
  };

  std::vector<char> buffer(64 * 1024);
  while (!stop_requested->load()) {
    const ssize_t length =
        read(inotify_fd, buffer.data(), static_cast<ssize_t>(buffer.size()));
    if (length < 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    bool queued = false;
    ssize_t offset = 0;
    while (offset < length) {
      const auto* event =
          reinterpret_cast<const inotify_event*>(buffer.data() + offset);
      offset += sizeof(inotify_event) + event->len;

      const auto it = watch_dirs.find(event->wd);
      if (it == watch_dirs.end()) {
        continue;
      }

      const fs::path dir = it->second;
      if (event->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) {
        watch_dirs.erase(it);
        inotify_rm_watch(inotify_fd, event->wd);
        continue;
      }

      if (event->len == 0) {
        continue;
      }

      const fs::path entry_path = dir / event->name;
      std::error_code ec;
      const fs::path rel = fs::relative(entry_path, root, ec);
      if (ec || rel.empty()) {
        continue;
      }
      const std::string rel_str = rel.generic_string();

      // IN_ISDIR is unreliable on DELETE; probe the filesystem for create/move-in.
      bool is_directory = (event->mask & IN_ISDIR) != 0;
      if (!is_directory && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
        is_directory = fs::is_directory(entry_path, ec);
      }

      if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
        // RemovePrefix covers files and directories (exact path + children). IN_ISDIR is
        // often absent on delete, so we always use the prefix form.
        if (should_list_workspace_path(rel_str, filter_options)) {
          FileIndexChange change;
          change.kind = FileIndexChangeKind::RemovePrefix;
          change.relative_path = rel_str;
          push_change(std::move(change));
          queued = true;
        }
        continue;
      }

      if (is_directory) {
        if ((event->mask & (IN_CREATE | IN_MOVED_TO)) &&
            !should_skip_dir_name(event->name, filter_options)) {
          scan_directories_for_watch(inotify_fd, root, entry_path, filter_options, &watch_dirs);
          FileIndexChange change;
          change.kind = FileIndexChangeKind::IndexDirectory;
          change.relative_path = rel_str;
          change.absolute_path = entry_path.string();
          push_change(std::move(change));
          queued = true;
        }
        continue;
      }

      if (!(event->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY))) {
        continue;
      }
      if (!should_list_workspace_path(rel_str, filter_options)) {
        continue;
      }

      FileIndexChange change;
      change.kind = FileIndexChangeKind::Upsert;
      change.relative_path = rel_str;
      change.absolute_path = entry_path.string();
      push_change(std::move(change));
      queued = true;
    }

    if (queued && on_changes) {
      on_changes();
    }
  }
}

#endif

}  // namespace

void rebuild_index_files_lower(IndexSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->files_lower.resize(snapshot->files.size());
  for (std::size_t i = 0; i < snapshot->files.size(); ++i) {
    snapshot->files_lower[i] = fuzzy_to_lower(snapshot->files[i]);
  }
}

namespace {

std::string catalog_filename(std::string_view path) {
  const std::size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return std::string(path);
  }
  return std::string(path.substr(slash + 1));
}

std::string catalog_dir_label(std::string_view label) {
  const std::size_t slash = label.find_last_of("/\\");
  if (slash == std::string::npos) {
    return {};
  }
  return std::string(label.substr(0, slash));
}

}  // namespace

void rebuild_index_file_picker_catalog(IndexSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  auto catalog = std::make_shared<std::vector<FilePickerCatalogEntry>>();
  catalog->reserve(snapshot->files.size());
  for (const std::string& path : snapshot->files) {
    FilePickerCatalogEntry entry;
    entry.path = path;
    entry.display_label = path;
    entry.filename = catalog_filename(path);
    entry.filename_lower = fuzzy_to_lower(entry.filename);
    entry.dir_label = catalog_dir_label(path);
    catalog->push_back(std::move(entry));
  }
  snapshot->file_picker_catalog = std::move(catalog);
}

void rebuild_index_derived_fields(IndexSnapshot* snapshot) {
  rebuild_index_files_lower(snapshot);
  rebuild_index_file_picker_catalog(snapshot);
}

std::vector<std::string> scan_workspace_files(const std::string& workspace_root,
                                                const IndexFilterOptions& filter_options) {
  std::vector<std::string> files;
  if (list_workspace_files_rg(workspace_root, filter_options, &files)) {
    return files;
  }

  std::error_code ec;
  const fs::path root(workspace_root);
  if (!fs::is_directory(root, ec)) {
    return files;
  }
  scan_dir(root, root, filter_options, &files);
  std::sort(files.begin(), files.end());
  return files;
}

WorkspaceIndexer::WorkspaceIndexer() {
  snapshot_ = std::make_shared<IndexSnapshot>();
}

WorkspaceIndexer::~WorkspaceIndexer() {
  stop();
}

void WorkspaceIndexer::start_scan(const std::string& workspace_root,
                                  const IndexFilterOptions& filter_options,
                                  const std::string& anchor_path,
                                  const std::string& open_file_path) {
  stop();
  stop_requested_ = false;
  scanning_ = true;
  {
    auto snap = std::make_shared<IndexSnapshot>();
    snap->workspace_root = workspace_root;
    snap->filter_options = filter_options;
    scan_workspace_skeleton(workspace_root, filter_options, anchor_path, open_file_path,
                            snap.get());
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  {
    std::lock_guard<std::mutex> lock(changes_mutex_);
    pending_changes_.clear();
  }
  worker_ = std::thread([this, workspace_root, filter_options] {
    set_current_thread_name("idx-work");
    worker_main(workspace_root, filter_options);
  });
}

void WorkspaceIndexer::stop() {
  stop_requested_ = true;
#if defined(__linux__)
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
#endif
  const pid_t rg_pid = rg_child_pid_.exchange(-1);
  if (rg_pid > 0) {
    kill(rg_pid, SIGTERM);
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  scanning_ = false;
#if defined(__linux__)
  inotify_fd_ = -1;
#endif
  {
    std::lock_guard<std::mutex> lock(changes_mutex_);
    pending_changes_.clear();
  }
}

std::shared_ptr<const IndexSnapshot> WorkspaceIndexer::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool WorkspaceIndexer::scanning() const {
  return scanning_.load();
}

void WorkspaceIndexer::set_change_notify(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(changes_mutex_);
  change_notify_ = std::move(callback);
}

bool WorkspaceIndexer::has_pending_changes() const {
  std::lock_guard<std::mutex> lock(changes_mutex_);
  return !pending_changes_.empty();
}

std::vector<FileIndexChange> WorkspaceIndexer::drain_changes() {
  std::lock_guard<std::mutex> lock(changes_mutex_);
  std::vector<FileIndexChange> out = std::move(pending_changes_);
  pending_changes_.clear();
  return out;
}

void WorkspaceIndexer::worker_main(std::string workspace_root,
                                     IndexFilterOptions filter_options) {
  TGDB_MON_SCOPE("idx", "workspace_indexer.scan");
  auto snap = std::make_shared<IndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->filter_options = filter_options;

  const auto should_cancel = [this]() { return stop_requested_.load(); };
  if (!list_workspace_files_rg(workspace_root, filter_options, &snap->files, should_cancel,
                               &rg_child_pid_)) {
    std::error_code ec;
    const fs::path root(workspace_root);
    if (fs::is_directory(root, ec)) {
      scan_dir(root, root, filter_options, &snap->files);
      std::sort(snap->files.begin(), snap->files.end());
    }
  }
  {
    std::error_code ec;
    const fs::path root(workspace_root);
    if (fs::is_directory(root, ec)) {
      collect_workspace_directories(root, root, filter_options, &snap->folders);
      sort_unique_strings(&snap->folders);
    }
  }
  rebuild_index_derived_fields(snap.get());
  TGDB_MON("idx", "workspace_indexer.files=" + std::to_string(snap->files.size()));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  scanning_ = false;

  if (stop_requested_.load()) {
    return;
  }

  // Avisa a la UI: el esqueleto ya no aplica y el explorador debe sincronizar el
  // snapshot completo (rg + carpetas). Sin esto el panel no se entera del cambio.
  {
    std::function<void()> notify;
    {
      std::lock_guard<std::mutex> lock(changes_mutex_);
      notify = change_notify_;
    }
    if (notify) {
      notify();
    }
  }

#if defined(__linux__)
  inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd_ < 0) {
    return;
  }
  const auto on_changes = [this]() {
    std::function<void()> notify;
    {
      std::lock_guard<std::mutex> lock(changes_mutex_);
      notify = change_notify_;
    }
    if (notify) {
      notify();
    }
  };
  run_inotify_loop(workspace_root, filter_options, inotify_fd_, &stop_requested_, &changes_mutex_,
                   &pending_changes_, on_changes);
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
#endif
}

void WorkspaceIndexer::upsert_file(const std::string& workspace_root,
                                   const std::string& relative_file,
                                   const std::string& absolute_path) {
  IndexFilterOptions options;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_) {
      options = snapshot_->filter_options;
    }
  }
  if (!should_list_workspace_path(relative_file, options)) {
    remove_file(workspace_root, relative_file);
    return;
  }
  std::error_code ec;
  if (!fs::is_regular_file(absolute_path, ec)) {
    remove_file(workspace_root, relative_file);
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
    updated->folders = snapshot_->folders;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());
  files.push_back(relative_file);
  std::sort(files.begin(), files.end());
  rebuild_index_derived_fields(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void WorkspaceIndexer::index_directory(const std::string& workspace_root,
                                       const std::string& relative_dir,
                                       const std::string& absolute_dir) {
  IndexFilterOptions options;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    options = snapshot_->filter_options;
  }

  std::error_code ec;
  const fs::path absolute = fs::absolute(absolute_dir, ec);
  if (!fs::is_directory(absolute, ec)) {
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
    updated->folders = snapshot_->folders;
  }

  std::function<void(const fs::path&, const fs::path&)> walk;
  walk = [&](const fs::path& abs_dir, const fs::path& rel_dir) {
    const std::string rel_str = rel_dir.empty() ? std::string{} : rel_dir.generic_string();
    if (!rel_str.empty()) {
      insert_sorted_unique(&updated->folders, rel_str);
    }

    for (const auto& entry : fs::directory_iterator(abs_dir, ec)) {
      if (ec) {
        break;
      }
      const auto name = entry.path().filename().string();
      if (name.empty() || name == "." || name == "..") {
        continue;
      }
      const fs::path rel = rel_dir.empty() ? fs::path(name) : rel_dir / name;
      const std::string entry_rel = rel.generic_string();
      if (entry.is_directory(ec)) {
        if (should_skip_dir_name(name, options)) {
          continue;
        }
        walk(entry.path(), rel);
      } else if (entry.is_regular_file(ec)) {
        if (!should_list_workspace_path(entry_rel, options)) {
          continue;
        }
        insert_sorted_unique(&updated->files, entry_rel);
      }
    }
  };

  const fs::path rel_dir = relative_dir.empty() ? fs::path{} : fs::path(relative_dir);
  walk(absolute, rel_dir);
  rebuild_index_derived_fields(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

void WorkspaceIndexer::remove_path_prefix(const std::string& workspace_root,
                                          const std::string& prefix) {
  if (prefix.empty()) {
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
    updated->folders = snapshot_->folders;
  }

  auto& files = updated->files;
  files.erase(std::remove_if(files.begin(), files.end(),
                             [&](const std::string& path) {
                               return path_matches_prefix(path, prefix);
                             }),
              files.end());

  auto& folders = updated->folders;
  folders.erase(std::remove_if(folders.begin(), folders.end(),
                                [&](const std::string& path) {
                                  return path_matches_prefix(path, prefix);
                                }),
               folders.end());

  rebuild_index_derived_fields(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

bool WorkspaceIndexer::refresh(const std::string& workspace_root) {
  IndexFilterOptions options;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return false;
    }
    options = snapshot_->filter_options;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  updated->workspace_root = workspace_root;
  updated->filter_options = options;
  updated->files = scan_workspace_files(workspace_root, options);

  std::error_code ec;
  const fs::path root(workspace_root);
  if (fs::is_directory(root, ec)) {
    collect_workspace_directories(root, root, options, &updated->folders);
    sort_unique_strings(&updated->folders);
  }

  rebuild_index_derived_fields(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
  return true;
}

void WorkspaceIndexer::remove_file(const std::string& workspace_root,
                                   const std::string& relative_file) {
  if (relative_file.empty()) {
    return;
  }

  auto updated = std::make_shared<IndexSnapshot>();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!snapshot_ || snapshot_->workspace_root != workspace_root) {
      return;
    }
    updated->workspace_root = workspace_root;
    updated->filter_options = snapshot_->filter_options;
    updated->files = snapshot_->files;
    updated->folders = snapshot_->folders;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());
  rebuild_index_derived_fields(updated.get());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

}  // namespace tgdb
