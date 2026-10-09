#pragma once

#include <string>
#include <vector>

namespace tuide {

struct IndexFilterOptions {
  bool show_all_files = false;
  // Prefijos relativos al workspace que el buscador de archivos (Ctrl+P) no lista.
  // El explorador y la navegación (p. ej. Ctrl+clic vía compile_commands) no los usan.
  std::vector<std::string> file_picker_exclude_dirs;
};

// Carpetas pesadas: nunca se indexan en profundidad; pueden mostrarse como stub.
bool is_lazy_stub_dir_name(const std::string& name);
// Stub visible en el explorador (p. ej. con show_all_files).
bool should_show_lazy_stub(const std::string& name, const IndexFilterOptions& options = {});

bool should_skip_dir_name(const std::string& name,
                          const IndexFilterOptions& options = {});
bool is_indexed_source_path(const std::string& path);
bool is_probably_binary_path(const std::string& path);
// LaTeX/build auxiliaries that churn heavily during compiles (.aux, .log, …).
bool is_build_noise_path(const std::string& path);
// Candidatos de Ctrl+P: excluye binarios conocidos; los PDF sí se incluyen (visor externo).
bool is_file_picker_candidate_path(const std::string& path);
// Deja la ruta relativa al workspace, sin barra final. Vacío si sale del workspace o no es usable.
std::string normalize_file_picker_exclude_dir(const std::string& workspace_root,
                                              const std::string& raw);
// true si relative_path es el directorio excluido o está debajo.
bool file_picker_path_excluded(const std::string& relative_path,
                               const std::vector<std::string>& exclude_dirs);
bool text_looks_binary(const std::string& text);
bool is_lsp_trackable_path(const std::string& path, const std::string& text = {});
bool is_cpp_header_path(const std::string& path);
std::vector<std::string> companion_source_paths_for_header(const std::string& header_path);
bool should_list_workspace_path(const std::string& relative_path,
                                const IndexFilterOptions& options = {});
// Deletes for noise paths still update the index (clear stale entries after a policy change).
bool should_track_workspace_delete(const std::string& relative_path,
                                   const IndexFilterOptions& options = {});
bool should_index_relative_path(const std::string& relative_path,
                                const IndexFilterOptions& options = {});

}  // namespace tuide
