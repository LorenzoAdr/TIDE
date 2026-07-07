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
FileTreeNode build_file_tree_from_paths_and_folders(
    const std::vector<std::string>& relative_paths,
    const std::vector<std::string>& relative_folders);

// Expande carpetas ancestras de relative_path (path relativo con '/').
// Devuelve true si el archivo existe en el árbol.
bool expand_relative_path(FileTreeNode* root, const std::string& relative_path);

}  // namespace tgdb
