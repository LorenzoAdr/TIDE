#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "indexer/index_rules.hpp"

namespace tgdb {

// Lists workspace files via `rg --files`. Returns false if rg is unavailable or fails.
bool list_workspace_files_rg(const std::string& workspace_root,
                             const IndexFilterOptions& filter_options,
                             std::vector<std::string>* out,
                             const std::function<bool()>& should_cancel = {},
                             std::atomic<pid_t>* child_pid = nullptr);

}  // namespace tgdb
