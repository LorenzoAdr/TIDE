#pragma once

#include <string>
#include <vector>

#include "app/workspace_config.hpp"

namespace tuide {

struct FileIndexerPaths {
  std::string compile_commands_dir;
  std::string status_note;
  bool entry_found = false;
  std::string entry_file;
  std::string entry_directory;
  std::string entry_output;
  std::vector<std::string> compile_arguments;
  std::vector<std::string> include_paths;
  std::vector<std::string> system_include_paths;
  std::vector<std::string> defines;
  std::vector<std::string> clangd_extra_flags;
  std::vector<std::string> display_lines;
};

FileIndexerPaths lookup_file_indexer_paths(const std::string& workspace_root,
                                           const WorkspaceConfig& config,
                                           const std::string& host_absolute_path);

}  // namespace tuide
