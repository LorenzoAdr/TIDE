#include "ui/git_diff_sync.hpp"

#include <filesystem>

#include "app/workspace_model.hpp"
#include "git/git_diff.hpp"
#include "git/git_service.hpp"
#include "ui/focus_manager.hpp"
#include "ui/main_layout.hpp"
#include "util/path_normalize.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

void align_initial_scroll(GitDiffSyncState* sync) {
  if (sync == nullptr || sync->working_ws == nullptr || sync->head_ws == nullptr) {
    return;
  }
  sync->syncing = true;
  const int working_scroll = sync->working_ws->buffer.scroll;
  const int mapped =
      map_git_scroll_line(sync->working_to_head, working_scroll);
  sync->working_ws->buffer.scroll = working_scroll;
  sync->head_ws->buffer.scroll = mapped;
  sync->last_working_scroll = working_scroll;
  sync->last_head_scroll = mapped;
  sync->working_ws->buffer.view_token++;
  sync->head_ws->buffer.view_token++;
  sync->working_ws->flush_active_tab();
  sync->head_ws->flush_active_tab();
  sync->syncing = false;
}

}  // namespace

void git_diff_sync_deactivate(MainLayoutState* layout) {
  if (layout == nullptr) {
    return;
  }
  layout->git_diff_sync = GitDiffSyncState{};
}

void git_diff_sync_refresh_map(MainLayoutState* layout) {
  if (layout == nullptr || !layout->git_diff_sync.active ||
      layout->git_diff_sync.working_ws == nullptr || layout->git_diff_sync.head_ws == nullptr) {
    return;
  }
  WorkspaceModel* working_ws = layout->git_diff_sync.working_ws;
  WorkspaceModel* head_ws = layout->git_diff_sync.head_ws;
  working_ws->ensure_buffer();
  head_ws->ensure_buffer();
  const GitLineMap map = build_git_line_map(head_ws->buffer.lines.to_vector(),
                                            working_ws->buffer.lines.to_vector());
  layout->git_diff_sync.working_to_head = map.working_to_head;
  layout->git_diff_sync.head_to_working = map.head_to_working;
}

bool open_git_diff_split_view(WorkspaceModel* working_ws, WorkspaceModel* head_ws, GitService* git,
                              MainLayoutState* layout, FocusManagerState* focus,
                              const std::string& workspace_rel_path) {
  if (working_ws == nullptr || head_ws == nullptr || git == nullptr || layout == nullptr ||
      workspace_rel_path.empty() || working_ws->root.empty()) {
    return false;
  }

  std::error_code ec;
  const fs::path absolute = fs::weakly_canonical(fs::path(working_ws->root) / workspace_rel_path, ec);
  if (ec) {
    return false;
  }
  const std::string abs_path = normalize_path(absolute.string());

  git->set_context_from_path(workspace_rel_path);
  git->refresh_file_diff(abs_path, true);
  git->refresh_file_head(abs_path);

  const GitFileDiff diff = git->file_diff(abs_path);
  if (!fs::is_regular_file(absolute, ec)) {
    if (diff.head_lines.empty()) {
      return false;
    }
  } else if (!working_ws->open_file(abs_path)) {
    return false;
  }

  head_ws->open_git_head_tab(abs_path, diff.head_lines);

  GitDiffSyncState sync;
  sync.active = true;
  sync.path = abs_path;
  sync.working_ws = working_ws;
  sync.head_ws = head_ws;
  const GitLineMap map =
      build_git_line_map(diff.head_lines, working_ws->buffer.lines.to_vector());
  sync.working_to_head = map.working_to_head;
  sync.head_to_working = map.head_to_working;
  layout->git_diff_sync = std::move(sync);
  align_initial_scroll(&layout->git_diff_sync);

  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  return true;
}

void git_diff_sync_on_scroll(MainLayoutState* layout, FocusRegion source) {
  if (layout == nullptr || !layout->git_diff_sync.active || layout->git_diff_sync.syncing) {
    return;
  }
  GitDiffSyncState& sync = layout->git_diff_sync;
  if (sync.working_ws == nullptr || sync.head_ws == nullptr) {
    return;
  }

  sync.syncing = true;
  if (source == FocusRegion::Editor) {
    sync.working_ws->ensure_buffer();
    const int line = sync.working_ws->buffer.scroll;
    if (line != sync.last_working_scroll) {
      sync.last_working_scroll = line;
      const int mapped = map_git_scroll_line(sync.working_to_head, line);
      sync.head_ws->ensure_buffer();
      if (sync.head_ws->buffer.scroll != mapped) {
        sync.head_ws->buffer.scroll = mapped;
        sync.last_head_scroll = mapped;
        sync.head_ws->buffer.view_token++;
        sync.head_ws->flush_active_tab();
      }
    }
  } else if (source == FocusRegion::SecondaryEditor) {
    sync.head_ws->ensure_buffer();
    const int line = sync.head_ws->buffer.scroll;
    if (line != sync.last_head_scroll) {
      sync.last_head_scroll = line;
      const int mapped = map_git_scroll_line(sync.head_to_working, line);
      sync.working_ws->ensure_buffer();
      if (sync.working_ws->buffer.scroll != mapped) {
        sync.working_ws->buffer.scroll = mapped;
        sync.last_working_scroll = mapped;
        sync.working_ws->buffer.view_token++;
        sync.working_ws->flush_active_tab();
      }
    }
  }
  sync.syncing = false;
}

}  // namespace tgdb
