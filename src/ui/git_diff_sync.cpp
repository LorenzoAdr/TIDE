#include "ui/git_diff_sync.hpp"

#include <filesystem>

#include "app/workspace_model.hpp"
#include "git/git_service.hpp"
#include "ui/focus_manager.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

bool open_git_diff_view(WorkspaceModel* workspace, GitService* git, FocusManagerState* focus,
                        const std::string& workspace_rel_path) {
  if (workspace == nullptr || git == nullptr || workspace_rel_path.empty() ||
      workspace->root.empty()) {
    return false;
  }

  std::error_code ec;
  const fs::path absolute = fs::weakly_canonical(fs::path(workspace->root) / workspace_rel_path, ec);
  if (ec) {
    return false;
  }
  const std::string abs_path = normalize_path(absolute.string());

  git->set_context_from_path(workspace_rel_path);
  if (!workspace->open_git_diff_tab(abs_path, git)) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  return true;
}

}  // namespace tgdb
