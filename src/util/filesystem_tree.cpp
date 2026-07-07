#include "util/filesystem_tree.hpp"

#include <algorithm>

namespace tgdb {

namespace {

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

FileTreeNode* find_or_create_folder(FileTreeNode* parent, const std::string& name) {
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

void insert_path(FileTreeNode* tree_root, const std::string& rel_str) {
  if (rel_str.empty()) {
    return;
  }

  FileTreeNode* parent = tree_root;
  const auto slash = rel_str.find_last_of('/');
  if (slash == std::string::npos) {
    FileTreeNode file;
    file.name = rel_str;
    file.relative_path = rel_str;
    file.is_file = true;
    parent->children.push_back(std::move(file));
    return;
  }

  std::size_t start = 0;
  while (start < slash) {
    const auto next = rel_str.find('/', start);
    const std::string part =
        rel_str.substr(start, next == std::string::npos ? std::string::npos : next - start);
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

void insert_folder_path(FileTreeNode* tree_root, const std::string& rel_str) {
  if (rel_str.empty()) {
    return;
  }

  FileTreeNode* parent = tree_root;
  std::size_t start = 0;
  while (start < rel_str.size()) {
    const auto next = rel_str.find('/', start);
    const std::string part =
        rel_str.substr(start, next == std::string::npos ? std::string::npos : next - start);
    if (part.empty()) {
      if (next == std::string::npos) {
        break;
      }
      start = next + 1;
      continue;
    }
    parent = find_or_create_folder(parent, part);
    if (next == std::string::npos) {
      break;
    }
    start = next + 1;
  }
}

}  // namespace

FileTreeNode build_file_tree_from_paths(const std::vector<std::string>& relative_paths) {
  FileTreeNode tree_root;
  tree_root.name = "";
  tree_root.is_file = false;
  tree_root.expanded = false;

  for (const auto& path : relative_paths) {
    insert_path(&tree_root, path);
  }

  sort_tree_nodes(&tree_root);
  return tree_root;
}

FileTreeNode build_file_tree_from_paths_and_folders(
    const std::vector<std::string>& relative_paths,
    const std::vector<std::string>& relative_folders) {
  FileTreeNode tree_root;
  tree_root.name = "";
  tree_root.is_file = false;
  tree_root.expanded = false;

  for (const auto& folder : relative_folders) {
    insert_folder_path(&tree_root, folder);
  }
  for (const auto& path : relative_paths) {
    insert_path(&tree_root, path);
  }

  sort_tree_nodes(&tree_root);
  return tree_root;
}

FileTreeNode* find_folder_child(FileTreeNode* parent, const std::string& name) {
  if (parent == nullptr) {
    return nullptr;
  }
  for (auto& child : parent->children) {
    if (!child.is_file && child.name == name) {
      return &child;
    }
  }
  return nullptr;
}

bool expand_relative_path(FileTreeNode* root, const std::string& relative_path) {
  if (root == nullptr || relative_path.empty()) {
    return false;
  }

  FileTreeNode* node = root;
  std::size_t start = 0;
  while (start <= relative_path.size()) {
    const std::size_t slash = relative_path.find('/', start);
    if (slash == std::string::npos) {
      for (const auto& child : node->children) {
        if (child.is_file && child.relative_path == relative_path) {
          return true;
        }
      }
      return false;
    }

    const std::string part = relative_path.substr(start, slash - start);
    node = find_folder_child(node, part);
    if (node == nullptr) {
      return false;
    }
    node->expanded = true;
    start = slash + 1;
  }
  return false;
}

}  // namespace tgdb
