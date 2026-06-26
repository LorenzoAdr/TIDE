#include "util/filesystem_tree.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace tgdb {

namespace {

bool should_skip_name(const std::string& name) {
  if (name.empty() || name[0] == '.') {
    return true;
  }
  if (name == "build" || name == "cmake-build-debug" ||
      name == "cmake-build-release" || name == "node_modules" ||
      name == "_deps") {
    return true;
  }
  return false;
}

bool is_source_file(const fs::path& path) {
  const auto ext = path.extension().string();
  return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" ||
         ext == ".hpp" || ext == ".c";
}

void sort_tree_nodes(FileTreeNode* node) {
  std::sort(node->children.begin(), node->children.end(),
            [](const FileTreeNode& a, const FileTreeNode& b) {
              if (a.is_file != b.is_file) {
                return !a.is_file;
              }
              return a.name < b.name;
            });
  for (auto& child : node->children) {
    if (!child.is_file) {
      sort_tree_nodes(&child);
    }
  }
}

FileTreeNode* find_or_create_folder(FileTreeNode* parent,
                                    const std::string& name) {
  for (auto& child : parent->children) {
    if (!child.is_file && child.name == name) {
      return &child;
    }
  }
  FileTreeNode folder;
  folder.name = name;
  folder.is_file = false;
  folder.expanded = false;
  parent->children.push_back(std::move(folder));
  return &parent->children.back();
}

}  // namespace

std::vector<std::string> build_file_tree(const std::string& root) {
  std::vector<std::string> entries;
  const FileTreeNode tree = build_file_tree_root(root);

  std::function<void(const FileTreeNode&)> collect;
  collect = [&](const FileTreeNode& node) {
    if (node.is_file) {
      entries.push_back(node.relative_path);
      return;
    }
    for (const auto& child : node.children) {
      collect(child);
    }
  };
  for (const auto& child : tree.children) {
    collect(child);
  }
  return entries;
}

FileTreeNode build_file_tree_root(const std::string& root) {
  FileTreeNode tree_root;
  tree_root.name = "";
  tree_root.is_file = false;
  tree_root.expanded = true;

  if (root.empty()) {
    return tree_root;
  }

  const fs::path root_path(root);
  if (!fs::exists(root_path) || !fs::is_directory(root_path)) {
    return tree_root;
  }

  std::error_code ec;
  for (const auto& entry : fs::recursive_directory_iterator(
           root_path, fs::directory_options::skip_permission_denied, ec)) {
    if (ec) {
      break;
    }
    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const fs::path rel = fs::relative(entry.path(), root_path, ec);
    if (ec || rel.empty()) {
      continue;
    }

    bool skip = false;
    for (const auto& part : rel) {
      if (should_skip_name(part.string())) {
        skip = true;
        break;
      }
    }
    if (skip || !is_source_file(entry.path())) {
      continue;
    }

    FileTreeNode* parent = &tree_root;
    const auto rel_str = rel.generic_string();
    const auto slash = rel_str.find_last_of('/');
    if (slash == std::string::npos) {
      FileTreeNode file;
      file.name = rel_str;
      file.relative_path = rel_str;
      file.is_file = true;
      parent->children.push_back(std::move(file));
      continue;
    }

    std::size_t start = 0;
    while (start < slash) {
      const auto next = rel_str.find('/', start);
      const std::string part =
          rel_str.substr(start, next == std::string::npos ? std::string::npos
                                                          : next - start);
      if (next == std::string::npos) {
        break;
      }
      parent = find_or_create_folder(parent, part);
      start = next + 1;
    }

    FileTreeNode file;
    file.name = rel_str.substr(slash + 1);
    file.relative_path = rel_str;
    file.is_file = true;
    parent->children.push_back(std::move(file));
  }

  sort_tree_nodes(&tree_root);
  return tree_root;
}

}  // namespace tgdb
