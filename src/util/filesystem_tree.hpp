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

FileTreeNode build_file_tree_from_paths(const std::vector<std::string>& relative_paths);

}  // namespace tgdb
