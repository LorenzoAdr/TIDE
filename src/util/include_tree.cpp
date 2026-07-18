#include "util/include_tree.hpp"

#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "indexer/index_rules.hpp"
#include "lsp/lsp_uri.hpp"

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string read_file_text(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::ostringstream out;
  out << input.rdbuf();
  return out.str();
}

bool path_exists(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

std::vector<std::pair<std::string, bool>> extract_includes(const std::string& text) {
  std::vector<std::pair<std::string, bool>> out;
  bool in_block_comment = false;

  std::size_t pos = 0;
  while (pos < text.size()) {
    if (in_block_comment) {
      const auto end = text.find("*/", pos);
      if (end == std::string::npos) {
        break;
      }
      pos = end + 2;
      in_block_comment = false;
      continue;
    }

    const auto line_end = text.find('\n', pos);
    const std::string_view line(text.data() + pos,
                                line_end == std::string::npos ? text.size() - pos
                                                              : line_end - pos);

    std::size_t i = 0;
    while (i < line.size()) {
      if (i + 1 < line.size() && line[i] == '/' && line[i + 1] == '*') {
        in_block_comment = true;
        i += 2;
        continue;
      }
      if (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/') {
        break;
      }
      if (line[i] == '#') {
        std::size_t j = i + 1;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) {
          ++j;
        }
        constexpr std::string_view kInclude = "include";
        if (j + kInclude.size() <= line.size() &&
            line.substr(j, kInclude.size()) == kInclude) {
          j += kInclude.size();
          while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j]))) {
            ++j;
          }
          if (j < line.size() && (line[j] == '"' || line[j] == '<')) {
            const char open = line[j];
            const char close = open == '"' ? '"' : '>';
            ++j;
            const std::size_t start = j;
            while (j < line.size() && line[j] != close) {
              ++j;
            }
            if (j < line.size() && j > start) {
              out.emplace_back(std::string(line.substr(start, j - start)), open == '<');
            }
          }
        }
      }
      ++i;
    }

    if (line_end == std::string::npos) {
      break;
    }
    pos = line_end + 1;
  }

  return out;
}

std::optional<std::string> resolve_quoted_include(const std::string& including_file,
                                                  const std::string& include_path) {
  if (include_path.empty()) {
    return std::nullopt;
  }
  const fs::path base = fs::path(including_file).parent_path();
  const fs::path candidate = base / include_path;
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(candidate, ec);
  if (!ec && path_exists(canonical.string())) {
    return normalize_lsp_path(canonical.string());
  }
  return std::nullopt;
}

std::optional<std::string> resolve_angle_include(const std::string& include_path,
                                                 const std::string& workspace_root,
                                                 const std::vector<std::string>& workspace_files) {
  if (include_path.empty() || workspace_root.empty()) {
    return std::nullopt;
  }

  std::optional<std::string> best;
  std::size_t best_suffix_len = 0;
  for (const auto& rel : workspace_files) {
    if (rel.size() < include_path.size()) {
      continue;
    }
    if (rel.compare(rel.size() - include_path.size(), include_path.size(), include_path) != 0) {
      continue;
    }
    if (rel.size() > include_path.size() && rel[rel.size() - include_path.size() - 1] != '/') {
      continue;
    }
    if (include_path.size() >= best_suffix_len) {
      best_suffix_len = include_path.size();
      best = normalize_lsp_path((fs::path(workspace_root) / rel).string());
    }
  }
  return best;
}

std::optional<std::string> resolve_include(const std::string& including_file,
                                           const std::string& include_path, bool is_system,
                                           const std::string& workspace_root,
                                           const std::vector<std::string>& workspace_files) {
  if (is_system) {
    return resolve_angle_include(include_path, workspace_root, workspace_files);
  }
  return resolve_quoted_include(including_file, include_path);
}

}  // namespace

std::unordered_set<std::string> build_include_tree(
    const std::string& root_file, const std::string& workspace_root,
    const std::vector<std::string>& workspace_relative_files,
    const std::string& root_text_override) {
  std::unordered_set<std::string> tree;
  const std::string root = normalize_lsp_path(root_file);
  if (root.empty() || !is_lsp_trackable_path(root)) {
    return tree;
  }

  tree.insert(root);
  std::deque<std::string> queue;
  queue.push_back(root);

  while (!queue.empty()) {
    const std::string current = queue.front();
    queue.pop_front();

    std::string text;
    if (current == root && !root_text_override.empty()) {
      text = root_text_override;
    } else {
      text = read_file_text(current);
    }
    if (text.empty()) {
      continue;
    }

    for (const auto& [include_path, is_system] : extract_includes(text)) {
      const auto resolved =
          resolve_include(current, include_path, is_system, workspace_root, workspace_relative_files);
      if (!resolved || !is_lsp_trackable_path(*resolved)) {
        continue;
      }
      if (tree.insert(*resolved).second) {
        queue.push_back(*resolved);
      }
    }
  }

  return tree;
}

}  // namespace tuide
