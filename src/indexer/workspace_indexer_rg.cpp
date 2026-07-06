#include "indexer/workspace_indexer_rg.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "indexer/index_rules.hpp"
#include "util/bundled_tools.hpp"
#include "util/child_process_guard.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string trim_line(std::string line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  return line;
}

void append_skip_dir_globs(std::vector<std::string>* args) {
  static constexpr const char* kSkipDirs[] = {
      "build",         "cmake-build-debug", "cmake-build-release", "node_modules",
      "_deps",         ".cache",            "dist",                "out",
  };
  for (const char* dir : kSkipDirs) {
    args->push_back("-g");
    args->push_back(std::string("!") + dir + "/**");
    args->push_back("-g");
    args->push_back(std::string("!**/") + dir + "/**");
  }
}

std::optional<std::string> relative_listing_path(const fs::path& workspace_root,
                                                   const std::string& raw_line) {
  if (raw_line.empty()) {
    return std::nullopt;
  }
  fs::path entry(raw_line);
  std::error_code ec;
  if (entry.is_absolute()) {
    const auto rel = fs::relative(entry, workspace_root, ec);
    if (ec || rel.empty()) {
      return std::nullopt;
    }
    return rel.generic_string();
  }
  std::string normalized = entry.generic_string();
  while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
    normalized = normalized.substr(2);
  }
  if (normalized == ".") {
    return std::nullopt;
  }
  return normalized;
}

}  // namespace

bool list_workspace_files_rg(const std::string& workspace_root,
                             const IndexFilterOptions& filter_options,
                             std::vector<std::string>* out,
                             const std::function<bool()>& should_cancel,
                             std::atomic<pid_t>* child_pid) {
  if (workspace_root.empty() || out == nullptr) {
    return false;
  }

  const auto rg = resolve_rg();
  if (!rg.has_value()) {
    return false;
  }

  std::error_code ec;
  const fs::path root(workspace_root);
  if (!fs::is_directory(root, ec)) {
    return false;
  }

  std::vector<std::string> args_storage;
  args_storage.emplace_back(rg->binary_path);
  args_storage.emplace_back("--files");
  args_storage.emplace_back("--threads");
  const unsigned hw = std::thread::hardware_concurrency();
  args_storage.emplace_back(std::to_string(hw > 0 ? hw : 4U));
  if (filter_options.show_all_files) {
    args_storage.emplace_back("--no-ignore");
    args_storage.emplace_back("--hidden");
  } else {
    append_skip_dir_globs(&args_storage);
  }
  args_storage.emplace_back(root.string());

  std::vector<char*> argv;
  argv.reserve(args_storage.size() + 1);
  for (auto& arg : args_storage) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return false;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return false;
  }

  if (pid == 0) {
    child_die_with_parent();
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    close(STDERR_FILENO);
    execv(rg->binary_path.c_str(), argv.data());
    _exit(127);
  }

  close(pipefd[1]);
  if (child_pid != nullptr) {
    child_pid->store(pid);
  }

  out->clear();
  std::vector<std::string> files;
  std::string pending;
  std::array<char, 8192> buffer{};
  bool cancelled = false;

  auto drain_lines = [&]() {
    std::size_t pos = 0;
    while (true) {
      const std::size_t newline = pending.find('\n', pos);
      if (newline == std::string::npos) {
        break;
      }
      const std::string line = trim_line(pending.substr(pos, newline - pos));
      pos = newline + 1;
      if (line.empty()) {
        continue;
      }
      const auto rel = relative_listing_path(root, line);
      if (!rel.has_value() || !should_list_workspace_path(*rel, filter_options)) {
        continue;
      }
      files.push_back(*rel);
    }
    if (pos > 0) {
      pending.erase(0, pos);
    }
  };

  while (true) {
    if (should_cancel && should_cancel()) {
      cancelled = true;
      kill(pid, SIGTERM);
      break;
    }

    const ssize_t nbytes = read(pipefd[0], buffer.data(), buffer.size());
    if (nbytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (nbytes == 0) {
      break;
    }
    pending.append(buffer.data(), static_cast<std::size_t>(nbytes));
    drain_lines();
  }

  if (!pending.empty()) {
    const std::string line = trim_line(pending);
    if (!line.empty()) {
      const auto rel = relative_listing_path(root, line);
      if (rel.has_value() && should_list_workspace_path(*rel, filter_options)) {
        files.push_back(*rel);
      }
    }
  }

  close(pipefd[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      break;
    }
  }

  if (child_pid != nullptr) {
    child_pid->store(-1);
  }

  if (cancelled) {
    return false;
  }
  if (!WIFEXITED(status)) {
    return false;
  }
  const int code = WEXITSTATUS(status);
  if (code != 0 && code != 1) {
    return false;
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  *out = std::move(files);
  return true;
}

}  // namespace tgdb
