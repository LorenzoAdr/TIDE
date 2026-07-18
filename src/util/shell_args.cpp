#include "util/shell_args.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string expand_tilde(std::string path) {
  if (path.empty() || path[0] != '~') {
    return path;
  }
  if (path.size() == 1 || path[1] == '/') {
    if (const char* home = std::getenv("HOME")) {
      return std::string(home) + path.substr(1);
    }
  }
  return path;
}

std::size_t last_token_start(const std::string& line) {
  bool in_single = false;
  bool in_double = false;
  bool escape = false;
  std::size_t start = 0;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (escape) {
      escape = false;
      continue;
    }
    if (ch == '\\' && in_double) {
      escape = true;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !in_single && !in_double) {
      start = i + 1;
    }
  }
  return start;
}

void split_token_dir_prefix(const std::string& token, std::string* dir, std::string* prefix) {
  if (token.empty()) {
    *dir = ".";
    *prefix = "";
    return;
  }
  const std::size_t slash = token.rfind('/');
  if (slash == std::string::npos) {
    *dir = ".";
    *prefix = token;
    return;
  }
  *dir = token.substr(0, slash + 1);
  *prefix = token.substr(slash + 1);
}

fs::path resolve_completion_directory(const std::string& cwd, const std::string& dir_token) {
  if (dir_token.empty() || dir_token == "./") {
    return fs::path(cwd);
  }
  std::string expanded = expand_tilde(dir_token);
  fs::path relative(expanded);
  if (relative.is_absolute()) {
    return relative;
  }
  std::error_code ec;
  const fs::path resolved = fs::weakly_canonical(fs::path(cwd) / relative, ec);
  return ec ? (fs::path(cwd) / relative) : resolved;
}

std::string longest_common_prefix(const std::vector<std::string>& values) {
  if (values.empty()) {
    return {};
  }
  std::string prefix = values.front();
  for (const std::string& value : values) {
    const std::size_t max_len = std::min(prefix.size(), value.size());
    std::size_t shared = 0;
    while (shared < max_len && prefix[shared] == value[shared]) {
      ++shared;
    }
    prefix.resize(shared);
    if (prefix.empty()) {
      break;
    }
  }
  return prefix;
}

}  // namespace

std::vector<std::string> split_shell_args(const std::string& command) {
  std::vector<std::string> tokens;
  std::string current;
  bool in_single = false;
  bool in_double = false;
  bool escape = false;
  for (char ch : command) {
    if (escape) {
      current.push_back(ch);
      escape = false;
      continue;
    }
    if (ch == '\\' && in_double) {
      escape = true;
      continue;
    }
    if (ch == '\'' && !in_double) {
      in_single = !in_single;
      continue;
    }
    if (ch == '"' && !in_single) {
      in_double = !in_double;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(ch)) && !in_single && !in_double) {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  return tokens;
}

std::string last_shell_token(const std::string& line) {
  const std::size_t start = last_token_start(line);
  return line.substr(start);
}

std::vector<std::string> path_completions(const std::string& cwd, const std::string& token) {
  std::vector<std::string> matches;
  if (cwd.empty()) {
    return matches;
  }

  std::string dir_token;
  std::string prefix;
  split_token_dir_prefix(token, &dir_token, &prefix);

  const fs::path directory = resolve_completion_directory(cwd, dir_token);
  std::error_code ec;
  if (!fs::is_directory(directory, ec)) {
    return matches;
  }

  for (const auto& entry : fs::directory_iterator(directory, ec)) {
    if (ec) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (name.empty() || name[0] == '.') {
      continue;
    }
    if (prefix.empty() || name.compare(0, prefix.size(), prefix) == 0) {
      std::string completed = dir_token + name;
      if (entry.is_directory(ec)) {
        completed.push_back('/');
      }
      matches.push_back(std::move(completed));
    }
  }

  std::sort(matches.begin(), matches.end());
  return matches;
}

bool apply_path_tab_completion(std::string* line, const std::string& cwd) {
  if (line == nullptr || cwd.empty()) {
    return false;
  }

  const std::size_t token_start = last_token_start(*line);
  const std::string token = line->substr(token_start);
  std::vector<std::string> matches = path_completions(cwd, token);
  if (matches.empty()) {
    return false;
  }

  std::string replacement;
  if (matches.size() == 1) {
    replacement = matches.front();
  } else {
    const std::string common = longest_common_prefix(matches);
    if (common.size() <= token.size()) {
      return false;
    }
    replacement = common;
  }

  line->replace(token_start, line->size() - token_start, replacement);
  return true;
}

}  // namespace tuide