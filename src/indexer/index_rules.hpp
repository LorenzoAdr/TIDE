#pragma once

#include <string>

namespace tgdb {

bool should_skip_dir_name(const std::string& name);
bool is_indexed_source_path(const std::string& path);
bool should_index_relative_path(const std::string& relative_path);

}  // namespace tgdb
