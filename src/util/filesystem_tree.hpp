#pragma once

#include <string>
#include <vector>

#include "indexer/index_rules.hpp"

namespace tuide {

struct FileTreeNode {
  std::string name;
  std::string relative_path;  // solo archivos
  bool is_file = false;
  bool expanded = false;
  // Stub pesado (build/, .git/, …): contenido se carga al expandir.
  bool lazy = false;
  bool children_loaded = true;
  std::vector<FileTreeNode> children;
};

FileTreeNode build_file_tree_from_paths(const std::vector<std::string>& relative_paths);
FileTreeNode build_file_tree_from_paths_and_folders(
    const std::vector<std::string>& relative_paths,
    const std::vector<std::string>& relative_folders);

// Marca carpetas cuyo basename es stub pesado (lazy, sin hijos cargados).
void mark_lazy_stub_folders(FileTreeNode* root, const IndexFilterOptions& options);

// Lista un nivel del directorio en disco y rellena children del nodo lazy.
bool populate_lazy_folder_children(FileTreeNode* folder, const std::string& workspace_root,
                                   const std::string& relative_dir,
                                   const IndexFilterOptions& options);

// Expande carpetas ancestras de relative_path (path relativo con '/').
// Devuelve true si el archivo existe en el árbol.
bool expand_relative_path(FileTreeNode* root, const std::string& relative_path);

}  // namespace tuide
