#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace tgdb {

// Returns normalized absolute paths: root_file plus all workspace headers reachable
// via #include (quoted and angle-bracket, resolved against workspace_root).
std::unordered_set<std::string> build_include_tree(
    const std::string& root_file, const std::string& workspace_root,
    const std::vector<std::string>& workspace_relative_files,
    const std::string& root_text_override = {});

}  // namespace tgdb
