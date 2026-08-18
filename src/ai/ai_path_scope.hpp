#pragma once

#include <string>
#include <vector>

namespace tuide {

// Normalize to a workspace-relative generic path (forward slashes, no trailing slash).
// Absolute paths under workspace_root become relative; others stay as normalized generic.
std::string ai_normalize_scope_path(const std::string& workspace_root, const std::string& path);

// Empty path_scope → unrestricted (true). Otherwise path must equal or live under a prefix.
bool ai_path_in_scope(const std::string& workspace_root, const std::string& path,
                      const std::vector<std::string>& path_scope);

// One-line prompt hint when scope is active; empty string when unrestricted.
std::string ai_path_scope_prompt_line(const std::vector<std::string>& path_scope);

}  // namespace tuide
