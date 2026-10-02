#include "ai/edit_snapshot.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "edit_snapshot_test failed: " << msg << '\n';
    std::abort();
  }
}

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  std::string out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return out;
}

void write_file(const fs::path& p, const std::string& content) {
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << content;
}

bool contains(const std::vector<std::string>& v, const std::string& needle) {
  for (const auto& s : v) {
    if (s == needle) {
      return true;
    }
  }
  return false;
}

bool any_prefix(const std::vector<std::string>& v, const std::string& prefix) {
  for (const auto& s : v) {
    if (s.rfind(prefix, 0) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  using namespace tuide;

  const fs::path root =
      fs::temp_directory_path() / ("tuide_edit_snapshot_test_" + std::to_string(::getpid()));
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  expect(fs::exists(root), "temp workspace root created");

  const std::string workspace_root = root.string();

  write_file(root / "a.txt", "hello\n");

  std::string err;
  expect(snapshot_ensure_repo(workspace_root, &err), ("ensure_repo ok: " + err).c_str());
  expect(fs::exists(fs::path(snapshot_dir(workspace_root)) / "HEAD"), "shadow gitdir initialized");

  const SnapshotResult baseline = snapshot_track(workspace_root, "baseline");
  expect(baseline.ok, ("baseline track ok: " + baseline.error).c_str());
  expect(!baseline.ref.empty(), "baseline ref non-empty");

  expect(snapshot_resolve_label(workspace_root, "baseline") == baseline.ref,
        "resolve_label(baseline) matches tracked ref");

  // Mutate the tree: change a.txt, add b.txt.
  write_file(root / "a.txt", "hello world\n");
  write_file(root / "b.txt", "new file\n");

  const SnapshotDiffResult diff_vs_worktree = snapshot_diff(workspace_root, baseline.ref);
  expect(diff_vs_worktree.ok, ("diff vs worktree ok: " + diff_vs_worktree.error).c_str());
  expect(contains(diff_vs_worktree.changed_paths, "a.txt"), "a.txt listed as changed");
  expect(contains(diff_vs_worktree.changed_paths, "b.txt"), "b.txt listed as changed");
  expect(diff_vs_worktree.diff.find("hello world") != std::string::npos,
        "diff text contains new content");
  expect(!any_prefix(diff_vs_worktree.changed_paths, ".tuide"),
        "shadow store itself never appears as a changed path");

  const SnapshotResult job1 = snapshot_track(workspace_root, "job:1");
  expect(job1.ok, ("job1 track ok: " + job1.error).c_str());
  expect(job1.ref != baseline.ref, "job1 ref differs from baseline");

  const SnapshotDiffResult diff_commits = snapshot_diff(workspace_root, baseline.ref, job1.ref);
  expect(diff_commits.ok, ("diff baseline..job1 ok: " + diff_commits.error).c_str());
  expect(contains(diff_commits.changed_paths, "a.txt"), "a.txt changed baseline..job1");
  expect(contains(diff_commits.changed_paths, "b.txt"), "b.txt changed baseline..job1");

  // Revert only a.txt back to baseline; b.txt must survive untouched.
  {
    std::string revert_err;
    const bool reverted =
        snapshot_revert(workspace_root, baseline.ref, {"a.txt"}, &revert_err);
    expect(reverted, ("revert a.txt ok: " + revert_err).c_str());
  }
  expect(read_file(root / "a.txt") == "hello\n", "a.txt content restored to baseline");
  expect(read_file(root / "b.txt") == "new file\n", "b.txt untouched by scoped revert");

  // Revert refuses an empty path list (no accidental wholesale checkout).
  {
    std::string revert_err;
    const bool reverted = snapshot_revert(workspace_root, baseline.ref, {}, &revert_err);
    expect(!reverted && !revert_err.empty(), "revert with empty paths is refused");
  }

  fs::remove_all(root, ec);

  std::cout << "edit_snapshot_test OK\n";
  return 0;
}
