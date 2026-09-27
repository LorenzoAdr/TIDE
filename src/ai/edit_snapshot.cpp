#include "ai/edit_snapshot.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "git/git_command.hpp"

namespace tuide {

namespace fs = std::filesystem;

namespace {

constexpr const char* kHeadRef = "refs/tuide/head";

std::string sanitize_label(const std::string& label) {
  std::string out;
  out.reserve(label.size());
  for (char c : label) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  if (out.empty()) {
    out = "snap";
  }
  return out;
}

std::string label_ref(const std::string& label) {
  return "refs/tuide/label/" + sanitize_label(label);
}

std::string trim_trailing_newline(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
    s.pop_back();
  }
  return s;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t nl = text.find('\n', start);
    if (nl == std::string::npos) {
      if (start < text.size()) {
        out.push_back(text.substr(start));
      }
      break;
    }
    if (nl > start) {
      out.push_back(text.substr(start, nl - start));
    }
    start = nl + 1;
  }
  return out;
}

bool repo_exists(const std::string& gitdir) {
  std::error_code ec;
  return !gitdir.empty() && fs::exists(fs::path(gitdir) / "HEAD", ec);
}

GitCommandResult run_shadow_git(const std::string& workspace_root, const std::string& gitdir,
                                const std::vector<std::string>& args) {
  std::vector<std::string> full;
  full.reserve(args.size() + 2);
  full.push_back("--git-dir=" + gitdir);
  full.push_back("--work-tree=" + workspace_root);
  for (const auto& a : args) {
    full.push_back(a);
  }
  return run_git(workspace_root, full);
}

}  // namespace

std::string snapshot_dir(const std::string& workspace_root) {
  if (workspace_root.empty()) {
    return {};
  }
  return (fs::path(workspace_root) / ".tuide" / "ai" / "snapshot" / "gitdir").string();
}

bool snapshot_ensure_repo(const std::string& workspace_root, std::string* err) {
  if (workspace_root.empty()) {
    if (err) {
      *err = "workspace_root vacío";
    }
    return false;
  }
  const std::string gitdir = snapshot_dir(workspace_root);
  if (repo_exists(gitdir)) {
    return true;
  }
  std::error_code ec;
  fs::create_directories(fs::path(gitdir).parent_path(), ec);
  const auto init = run_shadow_git(workspace_root, gitdir, {"init", "--quiet"});
  if (!init.success()) {
    if (err) {
      *err = "git init falló: " + trim_trailing_newline(init.stderr_text);
    }
    return false;
  }
  run_shadow_git(workspace_root, gitdir, {"config", "user.name", "tuide-snapshot"});
  run_shadow_git(workspace_root, gitdir, {"config", "user.email", "snapshot@tuide.local"});

  // Never let the shadow store (or build output) track itself, regardless of
  // whether the project has its own .gitignore.
  std::ofstream exclude_out((fs::path(gitdir) / "info" / "exclude").string(), std::ios::app);
  if (exclude_out) {
    exclude_out << "/.tuide/\n/build/\n/build-*/\n";
  }
  return true;
}

SnapshotResult snapshot_track(const std::string& workspace_root, const std::string& label) {
  SnapshotResult out;
  if (!snapshot_ensure_repo(workspace_root, &out.error)) {
    return out;
  }
  const std::string gitdir = snapshot_dir(workspace_root);

  const auto add = run_shadow_git(workspace_root, gitdir, {"add", "-A"});
  if (!add.success()) {
    out.error = "git add falló: " + trim_trailing_newline(add.stderr_text);
    return out;
  }
  const auto tree = run_shadow_git(workspace_root, gitdir, {"write-tree"});
  if (!tree.success()) {
    out.error = "write-tree falló: " + trim_trailing_newline(tree.stderr_text);
    return out;
  }
  const std::string tree_hash = trim_trailing_newline(tree.stdout_text);

  std::string parent;
  const auto parent_rev =
      run_shadow_git(workspace_root, gitdir, {"rev-parse", "--verify", "--quiet", kHeadRef});
  if (parent_rev.success()) {
    parent = trim_trailing_newline(parent_rev.stdout_text);
  }

  std::vector<std::string> commit_args = {"commit-tree", tree_hash};
  if (!parent.empty()) {
    commit_args.push_back("-p");
    commit_args.push_back(parent);
  }
  commit_args.push_back("-m");
  commit_args.push_back(label.empty() ? std::string("snapshot") : label);

  const auto commit = run_shadow_git(workspace_root, gitdir, commit_args);
  if (!commit.success()) {
    out.error = "commit-tree falló: " + trim_trailing_newline(commit.stderr_text);
    return out;
  }
  const std::string commit_hash = trim_trailing_newline(commit.stdout_text);
  if (commit_hash.empty()) {
    out.error = "commit-tree devolvió hash vacío";
    return out;
  }

  const auto upd_head =
      run_shadow_git(workspace_root, gitdir, {"update-ref", kHeadRef, commit_hash});
  if (!upd_head.success()) {
    out.error = "update-ref head falló: " + trim_trailing_newline(upd_head.stderr_text);
    return out;
  }
  run_shadow_git(workspace_root, gitdir, {"update-ref", label_ref(label), commit_hash});

  out.ok = true;
  out.ref = commit_hash;
  return out;
}

std::string snapshot_resolve_label(const std::string& workspace_root, const std::string& label) {
  const std::string gitdir = snapshot_dir(workspace_root);
  if (!repo_exists(gitdir)) {
    return {};
  }
  const auto rev =
      run_shadow_git(workspace_root, gitdir, {"rev-parse", "--verify", "--quiet", label_ref(label)});
  if (!rev.success()) {
    return {};
  }
  return trim_trailing_newline(rev.stdout_text);
}

SnapshotDiffResult snapshot_diff(const std::string& workspace_root, const std::string& from_ref,
                                 const std::string& to_ref, const std::vector<std::string>& paths) {
  SnapshotDiffResult out;
  if (from_ref.empty()) {
    out.error = "from_ref vacío";
    return out;
  }
  const std::string gitdir = snapshot_dir(workspace_root);
  if (!repo_exists(gitdir)) {
    out.error = "sin snapshot repo (nunca se llamó snapshot_track)";
    return out;
  }

  if (to_ref.empty()) {
    // Comparing against the live work-tree: plain `git diff <tree>` ignores
    // untracked files. Register their paths (empty blob, no content copy) so
    // brand-new files since `from_ref` show up as additions too.
    run_shadow_git(workspace_root, gitdir, {"add", "--intent-to-add", "-A"});
  }

  auto build_args = [&](bool name_only) {
    std::vector<std::string> args = {"diff", "--no-color"};
    if (name_only) {
      args.push_back("--name-only");
    }
    args.push_back(from_ref);
    if (!to_ref.empty()) {
      args.push_back(to_ref);
    }
    if (!paths.empty()) {
      args.push_back("--");
      for (const auto& p : paths) {
        args.push_back(p);
      }
    }
    return args;
  };

  const auto diff = run_shadow_git(workspace_root, gitdir, build_args(false));
  if (diff.exit_code != 0) {
    out.error = "git diff falló: " + trim_trailing_newline(diff.stderr_text);
    return out;
  }
  out.ok = true;
  out.diff = diff.stdout_text;

  const auto names = run_shadow_git(workspace_root, gitdir, build_args(true));
  if (names.exit_code == 0) {
    out.changed_paths = split_lines(names.stdout_text);
  }
  return out;
}

bool snapshot_revert(const std::string& workspace_root, const std::string& ref,
                     const std::vector<std::string>& paths, std::string* err) {
  if (ref.empty()) {
    if (err) {
      *err = "ref vacío";
    }
    return false;
  }
  if (paths.empty()) {
    if (err) {
      *err = "revert exige paths explícitos (resuélvelos con snapshot_diff)";
    }
    return false;
  }
  const std::string gitdir = snapshot_dir(workspace_root);
  if (!repo_exists(gitdir)) {
    if (err) {
      *err = "sin snapshot repo";
    }
    return false;
  }
  std::vector<std::string> args = {"checkout", ref, "--"};
  for (const auto& p : paths) {
    args.push_back(p);
  }
  const auto co = run_shadow_git(workspace_root, gitdir, args);
  if (!co.success()) {
    if (err) {
      *err = "git checkout falló: " + trim_trailing_newline(co.stderr_text);
    }
    return false;
  }
  return true;
}

}  // namespace tuide
