#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct FileTreeNode {
  std::string name;
  std::string relative_path;  // solo archivos
  bool is_file = false;
  bool expanded = false;
  std::vector<FileTreeNode> children;
};

std::vector<std::string> build_file_tree(const std::string& root);
FileTreeNode build_file_tree_root(const std::string& root);

}  // namespace tgdb
