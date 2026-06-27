#include "indexer/workspace_indexer.hpp"

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

bool should_skip_dir(const std::string& name) {
  if (name.empty() || name[0] == '.') {
    return true;
  }
  return name == "build" || name == "cmake-build-debug" ||
         name == "cmake-build-release" || name == "node_modules" ||
         name == "_deps" || name == ".cache" || name == "dist" || name == "out";
}

bool is_source_file(const fs::path& path) {
  const auto ext = path.extension().string();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
         ext == ".hpp" || ext == ".c";
}

void scan_dir(const fs::path& root, const fs::path& current,
              std::vector<std::string>* out) {
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    const auto name = entry.path().filename().string();
    if (entry.is_directory(ec)) {
      if (should_skip_dir(name)) {
        continue;
      }
      scan_dir(root, entry.path(), out);
    } else if (entry.is_regular_file(ec) && is_source_file(entry.path())) {
      std::error_code rel_ec;
      const auto rel = fs::relative(entry.path(), root, rel_ec);
      if (!rel_ec) {
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
  worker_ = std::thread([this, workspace_root] { worker_main(workspace_root); });
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
  auto snap = std::make_shared<IndexSnapshot>();
  snap->workspace_root = workspace_root;
  snap->files = scan_workspace_files(workspace_root);
  for (const auto& file : snap->files) {
    snap->trie.insert_path_tokens(file);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = snap;
  }
  running_ = false;
}

}  // namespace tgdb
