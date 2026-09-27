#pragma once

#include <string>
#include <vector>

namespace tuide {

// Shadow git history for AI edits.
//
// A git repository separate from the project's own `.git` (if any), whose
// `--git-dir` lives at `.tuide/ai/snapshot/gitdir` but whose `--work-tree` is
// the real workspace root. This lets the admin harness snapshot the tree
// before/after an edit and later diff or revert against those snapshots,
// without touching the user's real git state (staging area, HEAD, branches)
// and without requiring the opened project to be a git repo at all.
//
// Snapshots are plain commits (parent = previous snapshot) so `git diff`/
// `git checkout` work against them directly; there is no branch checkout
// involved, so this never conflicts with whatever the user's own git repo
// (if any) is doing concurrently.

struct SnapshotResult {
  bool ok = false;
  std::string ref;  // commit hash on success
  std::string error;
};

struct SnapshotDiffResult {
  bool ok = false;
  std::string diff;  // unified diff text (may be empty if nothing changed)
  std::vector<std::string> changed_paths;
  std::string error;
};

// Path to the shadow git-dir under workspace_root (does not imply it exists).
std::string snapshot_dir(const std::string& workspace_root);

// Idempotent: creates the shadow repo (git init) + local user.name/email +
// info/exclude (.tuide/, build/, build-*/ — never snapshot the snapshot store
// itself or build output) the first time it's called for this workspace.
bool snapshot_ensure_repo(const std::string& workspace_root, std::string* err);

// Stages the whole work-tree (`git add -A`; respects info/exclude above and
// the project's own .gitignore if any), writes a tree, wraps it in a commit
// whose parent is the previous snapshot (refs/tuide/head), and moves both
// refs/tuide/head and refs/tuide/label/<sanitized label> to it. `label` is
// free text (e.g. "baseline", "job:7"); it is sanitized for the ref name but
// kept verbatim as the commit message.
SnapshotResult snapshot_track(const std::string& workspace_root, const std::string& label);

// Resolves refs/tuide/label/<label> to its commit hash ("" if never tracked).
std::string snapshot_resolve_label(const std::string& workspace_root, const std::string& label);

// `git diff <from_ref> [<to_ref>] -- <paths>`. An empty `to_ref` compares
// from_ref's tree against the CURRENT on-disk work-tree — no need to
// track() first to see "what changed since <from_ref>".
SnapshotDiffResult snapshot_diff(const std::string& workspace_root, const std::string& from_ref,
                                 const std::string& to_ref = {},
                                 const std::vector<std::string>& paths = {});

// `git checkout <ref> -- <paths>` against the real work-tree: overwrites the
// current contents of `paths` with their contents at `ref`. `paths` must be
// non-empty (callers should resolve them from snapshot_diff(...).changed_paths
// first) so a typo'd/empty ref can never wipe the whole tree.
bool snapshot_revert(const std::string& workspace_root, const std::string& ref,
                     const std::vector<std::string>& paths, std::string* err);

}  // namespace tuide
