#include "search/workspace_search_rg.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/child_process_guard.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

constexpr int kMaxResults = 2000;

std::string trim_preview(std::string line) {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.pop_back();
  }
  const auto start = line.find_first_not_of(" \t");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = line.find_last_not_of(" \t");
  return line.substr(start, end - start + 1);
}

std::string normalize_filter(std::string value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.pop_back();
  }
  while (value.size() >= 2 && value[0] == '.' && value[1] == '/') {
    value = value.substr(2);
  }
  return value;
}

std::vector<std::string> split_patterns(const std::string& patterns) {
  std::vector<std::string> out;
  std::string current;
  for (const char c : patterns) {
    if (c == ',' || c == ';') {
      if (!current.empty()) {
        out.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(c);
    }
  }
  if (!current.empty()) {
    out.push_back(current);
  }
  return out;
}

void append_glob_args(const std::string& patterns, bool exclude,
                      std::vector<std::string>* args) {
  for (const auto& pattern : split_patterns(patterns)) {
    const std::string trimmed = normalize_filter(pattern);
    if (trimmed.empty()) {
      continue;
    }
    args->push_back("-g");
    args->push_back(exclude ? ("!" + trimmed) : trimmed);
  }
}

fs::path search_root_for(const WorkspaceSearchOptions& opts) {
  fs::path root(opts.workspace_root);
  const std::string filter = normalize_filter(opts.path_filter);
  if (!filter.empty()) {
    root /= filter;
  }
  return root;
}

std::optional<std::string> relative_result_path(const fs::path& workspace_root,
                                                const std::string& absolute_path) {
  std::error_code ec;
  const fs::path absolute = fs::weakly_canonical(fs::path(absolute_path), ec);
  if (ec) {
    return std::nullopt;
  }
  const fs::path root = fs::weakly_canonical(workspace_root, ec);
  if (ec) {
    return std::nullopt;
  }
  if (absolute == root) {
    return std::string{};
  }
  if (absolute.string().compare(0, root.string().size(), root.string()) == 0 &&
      absolute.string()[root.string().size()] == '/') {
    return fs::relative(absolute, root, ec).generic_string();
  }
  return absolute.filename().string();
}

bool parse_match_line(const std::string& line, const fs::path& workspace_root,
                      std::vector<WorkspaceSearchResult>* results, int* files_scanned) {
  nlohmann::json event;
  try {
    event = nlohmann::json::parse(line);
  } catch (const nlohmann::json::exception&) {
    return true;
  }

  const std::string type = event.value("type", "");
  if (type == "begin") {
    if (files_scanned != nullptr) {
      ++(*files_scanned);
    }
    return true;
  }
  if (type != "match" || !event.contains("data") || results == nullptr) {
    return true;
  }

  const auto& data = event["data"];
  if (!data.contains("path") || !data.contains("line_number")) {
    return true;
  }

  const std::string absolute_path = data["path"].value("text", std::string{});
  const auto rel = relative_result_path(workspace_root, absolute_path);
  if (!rel.has_value()) {
    return true;
  }

  int col = 1;
  if (data.contains("submatches") && data["submatches"].is_array() &&
      !data["submatches"].empty()) {
    col = data["submatches"][0].value("start", 0) + 1;
  }

  std::string preview;
  if (data.contains("lines") && data["lines"].is_object()) {
    preview = trim_preview(data["lines"].value("text", std::string{}));
  }

  results->push_back({*rel, data.value("line_number", 1), col, preview});
  return results->size() < static_cast<std::size_t>(kMaxResults);
}

}  // namespace

bool search_workspace_rg(const WorkspaceSearchOptions& opts, const std::string& rg_binary,
                         const std::function<bool()>& should_cancel,
                         std::atomic<pid_t>* child_pid, std::vector<WorkspaceSearchResult>* results,
                         int* files_scanned) {
  if (opts.needle.empty() || opts.workspace_root.empty() || rg_binary.empty() ||
      results == nullptr) {
    return false;
  }

  const fs::path root = search_root_for(opts);
  std::error_code ec;
  if (!fs::exists(root, ec)) {
    return false;
  }

  std::vector<std::string> args_storage;
  args_storage.emplace_back(rg_binary);
  args_storage.emplace_back("--json");
  args_storage.emplace_back("-F");
  args_storage.emplace_back("--max-count");
  args_storage.emplace_back(std::to_string(kMaxResults));
  args_storage.emplace_back("--threads");
  const unsigned hw = std::thread::hardware_concurrency();
  args_storage.emplace_back(std::to_string(hw > 0 ? hw : 4U));
  append_glob_args(opts.include_pattern, false, &args_storage);
  append_glob_args(opts.exclude_pattern, true, &args_storage);
  args_storage.emplace_back(opts.needle);
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
    execv(rg_binary.c_str(), argv.data());
    _exit(127);
  }

  close(pipefd[1]);
  if (child_pid != nullptr) {
    child_pid->store(pid);
  }

  results->clear();
  if (files_scanned != nullptr) {
    *files_scanned = 0;
  }

  std::string pending;
  std::array<char, 8192> buffer{};
  bool continue_search = true;
  bool cancelled = false;

  auto drain_lines = [&]() {
    std::size_t pos = 0;
    while (continue_search) {
      const std::size_t newline = pending.find('\n', pos);
      if (newline == std::string::npos) {
        break;
      }
      const std::string line = pending.substr(pos, newline - pos);
      pos = newline + 1;
      if (!parse_match_line(line, opts.workspace_root, results, files_scanned)) {
        continue_search = false;
        break;
      }
    }
    if (pos > 0) {
      pending.erase(0, pos);
    }
  };

  while (continue_search) {
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

  if (!pending.empty() && continue_search) {
    parse_match_line(pending, opts.workspace_root, results, files_scanned);
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
    return true;
  }
  if (WIFEXITED(status)) {
    const int code = WEXITSTATUS(status);
    return code == 0 || code == 1;
  }
  return false;
}

}  // namespace tgdb
