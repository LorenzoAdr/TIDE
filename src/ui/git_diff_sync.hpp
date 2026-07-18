#pragma once

#include <string>
#include <vector>

#include "ui/focus_manager.hpp"

#include "git/git_diff.hpp"

namespace tuide {

struct GitService;
struct FocusManagerState;
struct WorkspaceModel;

bool open_git_diff_view(WorkspaceModel* workspace, GitService* git, FocusManagerState* focus,
                        const std::string& workspace_rel_path);

}  // namespace tuide
