#include "indexer/workspace_indexer.hpp"

#include <algorithm>
#include <filesystem>
#include <sstream>

#include "indexer/index_rules.hpp"
#include "util/monitor_log.hpp"
#include "util/thread_name.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

void scan_dir(const fs::path& root, const fs::path& current,
              std::vector<std::string>* out) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      if (should_skip_dir_name(name)) {
        continue;
      }
      scan_dir(root, entry.path(), out);
    } else if (entry.is_regular_file(ec)) {
      std::error_code rel_ec;
      const auto rel = fs::relative(entry.path(), root, rel_ec);
      if (!rel_ec && should_list_workspace_path(rel.generic_string())) {
        out->push_back(rel.generic_string());
      }
    }
  }
}

}  // namespace

std::vector<std::string> scan_workspace_files(const std::string& workspace_root) {
  std::vector<std::string> files;
  std::error_code ec;
  const fs::path root(workspace_root);
  if (!fs::is_directory(root, ec)) {
    return files;
  }
  scan_dir(root, root, &files);
  std::sort(files.begin(), files.end());
  return files;
}

WorkspaceIndexer::WorkspaceIndexer() {
  snapshot_ = std::make_shared<IndexSnapshot>();
}

WorkspaceIndexer::~WorkspaceIndexer() {
  stop();
}

void WorkspaceIndexer::start_scan(const std::string& workspace_root) {
  stop();
  stop_requested_ = false;
  running_ = true;
  {
    auto snap = std::make_shared<IndexSnapshot>();
    snap->workspace_root = workspace_root;
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  worker_ = std::thread([this, workspace_root] {
    set_current_thread_name("idx-files");
    worker_main(workspace_root);
  });
}

void WorkspaceIndexer::stop() {
  stop_requested_ = true;
  if (worker_.joinable()) {
    worker_.join();
  }
  running_ = false;
}

std::shared_ptr<const IndexSnapshot> WorkspaceIndexer::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

bool WorkspaceIndexer::scanning() const {
  return running_.load();
}

void WorkspaceIndexer::worker_main(std::string workspace_root) {
  TGDB_MON_SCOPE("idx", "workspace_indexer.scan");
  auto snap = std::make_shared<IndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->files = scan_workspace_files(workspace_root);
  TGDB_MON("idx", "workspace_indexer.files=" + std::to_string(snap->files.size()));
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  running_ = false;
}

void WorkspaceIndexer::upsert_file(const std::string& workspace_root,
                                   const std::string& relative_file,
                                   const std::string& absolute_path) {
  if (!should_list_workspace_path(relative_file)) {
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
    updated->files = snapshot_->files;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());
  files.push_back(relative_file);
  std::sort(files.begin(), files.end());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
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
    updated->files = snapshot_->files;
  }

  auto& files = updated->files;
  files.erase(std::remove(files.begin(), files.end(), relative_file), files.end());

  std::lock_guard<std::mutex> lock(mutex_);
  snapshot_ = updated;
}

}  // namespace tgdb
