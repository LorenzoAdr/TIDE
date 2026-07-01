#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "search/workspace_search.hpp"

namespace tgdb {

// Busca con ripgrep (--json). Devuelve false si rg falla (el caller puede hacer fallback).
bool search_workspace_rg(const WorkspaceSearchOptions& opts, const std::string& rg_binary,
                         const std::function<bool()>& should_cancel,
                         std::atomic<pid_t>* child_pid, std::vector<WorkspaceSearchResult>* results,
                         int* files_scanned);

}  // namespace tgdb
