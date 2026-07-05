#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct IndexFilterOptions {
  bool show_all_files = false;
};

bool should_skip_dir_name(const std::string& name,
                          const IndexFilterOptions& options = {});
bool is_indexed_source_path(const std::string& path);
bool is_probably_binary_path(const std::string& path);
bool text_looks_binary(const std::string& text);
bool is_lsp_trackable_path(const std::string& path, const std::string& text = {});
bool is_cpp_header_path(const std::string& path);
std::vector<std::string> companion_source_paths_for_header(const std::string& header_path);
bool should_list_workspace_path(const std::string& relative_path,
                                const IndexFilterOptions& options = {});
bool should_index_relative_path(const std::string& relative_path,
                                const IndexFilterOptions& options = {});

}  // namespace tgdb
