#include "indexer/workspace_indexer.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

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

namespace tuide {

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
      if (should_show_lazy_stub(name, options)) {
        folders->push_back(rel_str);
        continue;
      }
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

// Ruta relativa léxica: no usar fs::relative (resuelve symlinks vía weakly_canonical
// y el enlace desaparece del explorador, p. ej. link -> src se colapsa a "src").
fs::path lexical_workspace_relative(const fs::path& root, const fs::path& absolute) {
  const fs::path rel = absolute.lexically_relative(root);
  if (rel.empty() || rel == "." || (!rel.empty() && *rel.begin() == "..")) {
    return {};
  }
  return rel;
}

void scan_dir(const fs::path& root, const fs::path& current, const fs::path& current_rel,
              const IndexFilterOptions& options, std::vector<std::string>* out) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (name.empty() || name == "." || name == "..") {
      continue;
    }
    const fs::path rel = current_rel.empty() ? fs::path(name) : current_rel / name;
    if (entry.is_directory(ec)) {
      if (should_skip_dir_name(name, options)) {
        continue;
      }
      // No descender por symlinks a carpetas: evita ciclos y rutas fuera del árbol.
      if (entry.is_symlink(ec)) {
        continue;
      }
      scan_dir(root, entry.path(), rel, options, out);
    } else if (entry.is_regular_file(ec)) {
      if (should_list_workspace_path(rel.generic_string(), options)) {
        out->push_back(rel.generic_string());
      }
    }
  }
}

void collect_workspace_directories(const fs::path& root, const fs::path& current,
                                   const fs::path& current_rel, const IndexFilterOptions& options,
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
    const fs::path rel = current_rel.empty() ? fs::path(name) : current_rel / name;
    folders->push_back(rel.generic_string());
    // El enlace se lista con su nombre; el contenido se carga al expandir (lazy).
    if (entry.is_symlink(ec)) {
      continue;
    }
    collect_workspace_directories(root, entry.path(), rel, options, folders);
  }
}

void append_lazy_stub_folders(const fs::path& root, const IndexFilterOptions& options,
                              std::vector<std::string>* folders) {
  if (folders == nullptr || !options.show_all_files) {
    return;
  }
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(root, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_directory(ec)) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (!should_show_lazy_stub(name, options)) {
      continue;
    }
    folders->push_back(name);
  }
}

}  // namespace

bool index_path_matches_prefix(const std::string& path, const std::string& prefix) {
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

namespace {

void coalesce_remove_prefixes(std::vector<std::string>* prefixes) {
  if (prefixes == nullptr || prefixes->size() <= 1) {
    return;
  }
  std::sort(prefixes->begin(), prefixes->end(),
            [](const std::string& a, const std::string& b) {
              if (a.size() != b.size()) {
                return a.size() < b.size();
              }
              return a < b;
            });
  prefixes->erase(std::unique(prefixes->begin(), prefixes->end()), prefixes->end());

  std::vector<std::string> kept;
  kept.reserve(prefixes->size());
  for (const std::string& prefix : *prefixes) {
    bool dominated = false;
    for (const std::string& parent : kept) {
      if (index_path_matches_prefix(prefix, parent)) {
        dominated = true;
        break;
      }
    }
    if (!dominated) {
      kept.push_back(prefix);
    }
  }
  *prefixes = std::move(kept);
}

}  // namespace

std::vector<FileIndexChange> coalesce_file_index_changes(std::vector<FileIndexChange> changes) {
  std::vector<FileIndexChange> out;
  out.reserve(changes.size());
  std::vector<std::string> pending_prefixes;
  std::vector<FileIndexChange> pending_upserts;
  bool pending_remove_wake = false;
  bool pending_upsert_wake = false;

  auto flush_removes = [&]() {
    if (pending_prefixes.empty()) {
      return;
    }
    coalesce_remove_prefixes(&pending_prefixes);
    for (std::string& prefix : pending_prefixes) {
      FileIndexChange change;
      change.kind = FileIndexChangeKind::RemovePrefix;
      change.relative_path = std::move(prefix);
      change.wake_ui = pending_remove_wake;
      out.push_back(std::move(change));
    }
    pending_prefixes.clear();
    pending_remove_wake = false;
  };

  auto flush_upserts = [&]() {
    if (pending_upserts.empty()) {
      return;
    }
    // Last write wins per relative path (stable order of first appearance).
    std::vector<FileIndexChange> unique;
    unique.reserve(pending_upserts.size());
    std::unordered_map<std::string, std::size_t> index_by_path;
    for (FileIndexChange& change : pending_upserts) {
      const auto it = index_by_path.find(change.relative_path);
      if (it == index_by_path.end()) {
        index_by_path.emplace(change.relative_path, unique.size());
        unique.push_back(std::move(change));
      } else {
        unique[it->second].absolute_path = std::move(change.absolute_path);
        unique[it->second].wake_ui = unique[it->second].wake_ui || change.wake_ui;
      }
    }
    for (FileIndexChange& change : unique) {
      change.wake_ui = pending_upsert_wake || change.wake_ui;
      out.push_back(std::move(change));
    }
    pending_upserts.clear();
    pending_upsert_wake = false;
  };

  for (FileIndexChange& change : changes) {
    if (change.kind == FileIndexChangeKind::Remove ||
        change.kind == FileIndexChangeKind::RemovePrefix) {
      flush_upserts();
      if (!change.relative_path.empty()) {
        pending_prefixes.push_back(std::move(change.relative_path));
        pending_remove_wake = pending_remove_wake || change.wake_ui;
      }
      continue;
    }
    if (change.kind == FileIndexChangeKind::Upsert) {
      flush_removes();
      if (!change.relative_path.empty()) {
        pending_upsert_wake = pending_upsert_wake || change.wake_ui;
        pending_upserts.push_back(std::move(change));
      }
      continue;
    }
    flush_removes();
    flush_upserts();
    out.push_back(std::move(change));
  }
  flush_removes();
  flush_upserts();
  return out;
}

namespace {

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
    // No vigilar a través de symlinks (ciclos / rutas externas).
    if (entry.is_symlink(ec)) {
      continue;
    }
    scan_directories_for_watch(fd, root, entry.path(), options, watch_dirs);
  }
}

void run_inotify_loop(const std::string& workspace_root, const IndexFilterOptions& filter_options,
                      int inotify_fd, std::atomic<bool>* stop_requested,
                      std::mutex* changes_mutex, std::vector<FileIndexChange>* pending_changes,
                      const std::function<void(bool wake_ui)>& on_changes,
                      const std::function<bool(const std::string&)>& modify_should_wake) {
  std::unordered_map<int, fs::path> watch_dirs;
  const fs::path root(workspace_root);
  scan_directories_for_watch(inotify_fd, root, root, filter_options, &watch_dirs);

  auto push_change = [&](FileIndexChange change) {
    {
      std::lock_guard<std::mutex> lock(*changes_mutex);
      pending_changes->push_back(std::move(change));
    }
  };

  const auto kQuiet = std::chrono::milliseconds(kIndexerFsChangeDebounceMs);
  const auto kMaxWait = std::chrono::milliseconds(kIndexerFsChangeMaxDebounceMs);
  bool debounce_pending = false;
  bool debounce_wake_ui = false;
  auto last_event_time = std::chrono::steady_clock::now();
  auto storm_start_time = last_event_time;

  auto flush_notify = [&]() {
    if (!debounce_pending || !on_changes) {
      debounce_pending = false;
      debounce_wake_ui = false;
      return;
    }
    const bool wake = debounce_wake_ui;
    debounce_pending = false;
    debounce_wake_ui = false;
    on_changes(wake);
  };

  std::vector<char> buffer(64 * 1024);
  while (!stop_requested->load()) {
    const ssize_t length =
        read(inotify_fd, buffer.data(), static_cast<ssize_t>(buffer.size()));
    const auto now = std::chrono::steady_clock::now();
    if (length < 0) {
      if (debounce_pending &&
          (now - last_event_time >= kQuiet || now - storm_start_time >= kMaxWait)) {
        flush_notify();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    bool queued = false;
    bool wake_ui = false;
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
      const fs::path rel = lexical_workspace_relative(root, entry_path);
      if (rel.empty()) {
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
        if (should_track_workspace_delete(rel_str, filter_options)) {
          FileIndexChange change;
          change.kind = FileIndexChangeKind::RemovePrefix;
          change.relative_path = rel_str;
          change.wake_ui = true;
          push_change(std::move(change));
          queued = true;
          wake_ui = true;
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
          change.wake_ui = true;
          push_change(std::move(change));
          queued = true;
          wake_ui = true;
        }
        continue;
      }

      if (!(event->mask & (IN_CREATE | IN_MOVED_TO | IN_MODIFY))) {
        continue;
      }
      if (!should_list_workspace_path(rel_str, filter_options)) {
        continue;
      }

      const bool is_modify_only =
          (event->mask & IN_MODIFY) != 0 &&
          (event->mask & (IN_CREATE | IN_MOVED_TO)) == 0;
      const std::string abs_str = entry_path.string();
      if (is_modify_only) {
        // Content-only writes: wake only if the file is visible in an editor so
        // reload_stale_tabs_from_disk can run. Otherwise queue sources silently
        // for symbol reindex, and ignore noise (logs, etc.).
        const bool editor_visible = modify_should_wake && modify_should_wake(abs_str);
        if (editor_visible) {
          // fall through — queue with wake_ui
        } else if (should_index_relative_path(rel_str, filter_options)) {
          FileIndexChange change;
          change.kind = FileIndexChangeKind::Upsert;
          change.relative_path = rel_str;
          change.absolute_path = abs_str;
          change.wake_ui = false;
          push_change(std::move(change));
          queued = true;
          continue;
        } else {
          continue;
        }
      }

      FileIndexChange change;
      change.kind = FileIndexChangeKind::Upsert;
      change.relative_path = rel_str;
      change.absolute_path = abs_str;
      change.wake_ui = true;
      push_change(std::move(change));
      queued = true;
      wake_ui = true;
    }

    if (queued) {
      if (!debounce_pending) {
        storm_start_time = now;
      }
      debounce_pending = true;
      last_event_time = now;
      debounce_wake_ui = debounce_wake_ui || wake_ui;
      if (now - storm_start_time >= kMaxWait) {
        flush_notify();
      }
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
    if (!is_file_picker_candidate_path(path)) {
      continue;
    }
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
  scan_dir(root, root, {}, filter_options, &files);
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

void WorkspaceIndexer::set_change_notify(std::function<void(bool wake_ui)> callback) {
  std::lock_guard<std::mutex> lock(changes_mutex_);
  change_notify_ = std::move(callback);
}

void WorkspaceIndexer::set_modify_wake_predicate(
    std::function<bool(const std::string& absolute_path)> pred) {
  std::lock_guard<std::mutex> lock(modify_wake_mutex_);
  modify_wake_predicate_ = std::move(pred);
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
  TUIDE_MON_SCOPE("idx", "workspace_indexer.scan");
  auto snap = std::make_shared<IndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->filter_options = filter_options;

  const auto should_cancel = [this]() { return stop_requested_.load(); };
  if (!list_workspace_files_rg(workspace_root, filter_options, &snap->files, should_cancel,
                               &rg_child_pid_)) {
    std::error_code ec;
    const fs::path root(workspace_root);
    if (fs::is_directory(root, ec)) {
      scan_dir(root, root, {}, filter_options, &snap->files);
      std::sort(snap->files.begin(), snap->files.end());
    }
  }
  {
    std::error_code ec;
    const fs::path root(workspace_root);
    if (fs::is_directory(root, ec)) {
      collect_workspace_directories(root, root, {}, filter_options, &snap->folders);
      append_lazy_stub_folders(root, filter_options, &snap->folders);
      sort_unique_strings(&snap->folders);
    }
  }
  rebuild_index_derived_fields(snap.get());
  TUIDE_MON("idx", "workspace_indexer.files=" + std::to_string(snap->files.size()));
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
    std::function<void(bool)> notify;
    {
      std::lock_guard<std::mutex> lock(changes_mutex_);
      notify = change_notify_;
    }
    if (notify) {
      notify(true);
    }
  }

#if defined(__linux__)
  inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd_ < 0) {
    return;
  }
  const auto on_changes = [this](bool wake_ui) {
    std::function<void(bool)> notify;
    {
      std::lock_guard<std::mutex> lock(changes_mutex_);
      notify = change_notify_;
    }
    if (notify) {
      notify(wake_ui);
    }
  };
  const auto modify_should_wake = [this](const std::string& absolute_path) {
    std::function<bool(const std::string&)> pred;
    {
      std::lock_guard<std::mutex> lock(modify_wake_mutex_);
      pred = modify_wake_predicate_;
    }
    return pred && pred(absolute_path);
  };
  run_inotify_loop(workspace_root, filter_options, inotify_fd_, &stop_requested_, &changes_mutex_,
                   &pending_changes_, on_changes, modify_should_wake);
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }
#endif
}

void WorkspaceIndexer::upsert_file(const std::string& workspace_root,
                                   const std::string& relative_file,
                                   const std::string& absolute_path) {
  FileIndexChange change;
  change.kind = FileIndexChangeKind::Upsert;
  change.relative_path = relative_file;
  change.absolute_path = absolute_path;
  upsert_files(workspace_root, std::vector<FileIndexChange>{std::move(change)});
}

void WorkspaceIndexer::upsert_files(const std::string& workspace_root,
                                    const std::vector<FileIndexChange>& upserts) {
  if (upserts.empty()) {
    return;
  }

  IndexFilterOptions options;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot_) {
      options = snapshot_->filter_options;
    }
  }

  std::vector<std::string> to_add;
  std::vector<std::string> to_remove;
  to_add.reserve(upserts.size());
  for (const FileIndexChange& change : upserts) {
    if (change.relative_path.empty()) {
      continue;
    }
    if (!should_list_workspace_path(change.relative_path, options)) {
      to_remove.push_back(change.relative_path);
      continue;
    }
    std::error_code ec;
    if (change.absolute_path.empty() || !fs::is_regular_file(change.absolute_path, ec)) {
      to_remove.push_back(change.relative_path);
      continue;
    }
    to_add.push_back(change.relative_path);
  }

  if (to_add.empty() && to_remove.empty()) {
    return;
  }

  // Dedupe adds (last occurrence wins) while preserving sorted insert via set erase+push.
  std::unordered_map<std::string, std::size_t> add_index;
  std::vector<std::string> unique_adds;
  unique_adds.reserve(to_add.size());
  for (std::string& path : to_add) {
    const auto it = add_index.find(path);
    if (it == add_index.end()) {
      add_index.emplace(path, unique_adds.size());
      unique_adds.push_back(std::move(path));
    } else {
      unique_adds[it->second] = std::move(path);
    }
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
  if (!to_remove.empty() || !unique_adds.empty()) {
    std::unordered_set<std::string> drop;
    drop.reserve(to_remove.size() + unique_adds.size());
    for (const std::string& path : to_remove) {
      drop.insert(path);
    }
    for (const std::string& path : unique_adds) {
      drop.insert(path);
    }
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const std::string& path) { return drop.count(path) > 0; }),
                files.end());
  }
  files.insert(files.end(), unique_adds.begin(), unique_adds.end());
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
        if (is_lazy_stub_dir_name(name)) {
          if (should_show_lazy_stub(name, options)) {
            insert_sorted_unique(&updated->folders, entry_rel);
          }
          continue;
        }
        if (should_skip_dir_name(name, options)) {
          continue;
        }
        // Symlink a carpeta: listar el enlace, no indexar el destino (lazy al expandir).
        if (entry.is_symlink(ec)) {
          insert_sorted_unique(&updated->folders, entry_rel);
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
  remove_path_prefixes(workspace_root, std::vector<std::string>{prefix});
}

void WorkspaceIndexer::remove_path_prefixes(const std::string& workspace_root,
                                            const std::vector<std::string>& prefixes) {
  if (prefixes.empty()) {
    return;
  }
  std::vector<std::string> effective = prefixes;
  coalesce_remove_prefixes(&effective);
  if (effective.empty()) {
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

  const auto matches_any = [&](const std::string& path) {
    for (const std::string& prefix : effective) {
      if (index_path_matches_prefix(path, prefix)) {
        return true;
      }
    }
    return false;
  };

  auto& files = updated->files;
  files.erase(std::remove_if(files.begin(), files.end(), matches_any), files.end());

  auto& folders = updated->folders;
  folders.erase(std::remove_if(folders.begin(), folders.end(), matches_any), folders.end());

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
    collect_workspace_directories(root, root, {}, options, &updated->folders);
    append_lazy_stub_folders(root, options, &updated->folders);
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

}  // namespace tuide
