#include "git/git_diff.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace tgdb {

namespace {

int parse_line_number(const std::string& hunk, char marker) {
  const std::string token = std::string(1, marker);
  const auto pos = hunk.find(token, 2);
  if (pos == std::string::npos) {
    return -1;
  }
  std::size_t start = pos + 1;
  const std::size_t comma = hunk.find(',', start);
  const std::size_t space = hunk.find(' ', start);
  const std::size_t end = comma != std::string::npos && (space == std::string::npos || comma < space)
                              ? comma
                              : space;
  const std::string num =
      end == std::string::npos ? hunk.substr(start) : hunk.substr(start, end - start);
  return std::atoi(num.c_str()) - 1;
}

}  // namespace

std::vector<std::string> parse_file_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

GitFileDiff parse_unified_diff(const std::string& path, const std::string& diff_output) {
  GitFileDiff diff;
  diff.path = path;
  diff.loaded = true;
  if (diff_output.empty()) {
    return diff;
  }

  int old_line = 0;
  int new_line = 0;
  bool in_hunk = false;
  std::string pending_removed;

  std::istringstream stream(diff_output);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("@@", 0) == 0) {
      old_line = parse_line_number(line, '-');
      new_line = parse_line_number(line, '+');
      in_hunk = old_line >= 0 && new_line >= 0;
      pending_removed.clear();
      continue;
    }
    if (!in_hunk || line.empty()) {
      continue;
    }
    if (line.rfind("\\ No newline at end of file", 0) == 0) {
      continue;
    }
    const char prefix = line[0];
    if (prefix != ' ' && prefix != '-' && prefix != '+') {
      continue;
    }
    const std::string content = line.size() > 1 ? line.substr(1) : std::string{};
    if (prefix == ' ') {
      pending_removed.clear();
      ++old_line;
      ++new_line;
    } else if (prefix == '-') {
      pending_removed = content;
      ++old_line;
    } else if (prefix == '+') {
      diff.line_changes[new_line] =
          pending_removed.empty() ? GitLineChange::kAdded : GitLineChange::kModified;
      if (!pending_removed.empty()) {
        diff.previous_content_by_line[new_line] = pending_removed;
        pending_removed.clear();
      }
      ++new_line;
    }
  }
  return diff;
}

LineDiffResult compute_line_diff(const std::vector<std::string>& head,
                                 const std::vector<std::string>& current) {
  LineDiffResult result;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < current.size() && j < head.size()) {
    if (current[i] == head[j]) {
      ++i;
      ++j;
      continue;
    }
    if (j + 1 < head.size() && current[i] == head[j + 1]) {
      ++j;
      continue;
    }
    if (i + 1 < current.size() && current[i + 1] == head[j]) {
      const int line = static_cast<int>(i);
      result.changed_new_lines.insert(line);
      ++i;
      continue;
    }
    const int line = static_cast<int>(i);
    result.changed_new_lines.insert(line);
    result.previous_content_by_new_line[line] = head[j];
    ++i;
    ++j;
  }
  while (i < current.size()) {
    result.changed_new_lines.insert(static_cast<int>(i));
    ++i;
  }
  return result;
}

std::unordered_set<int> compute_changed_lines(const std::vector<std::string>& current,
                                              const std::vector<std::string>& head) {
  return compute_line_diff(head, current).changed_new_lines;
}

GitLineMap build_git_line_map(const std::vector<std::string>& head,
                              const std::vector<std::string>& working) {
  GitLineMap map;
  map.working_to_head.assign(working.size(), -1);
  map.head_to_working.assign(head.size(), -1);

  std::size_t i = 0;
  std::size_t j = 0;
  while (i < working.size() && j < head.size()) {
    if (working[i] == head[j]) {
      map.working_to_head[i] = static_cast<int>(j);
      map.head_to_working[j] = static_cast<int>(i);
      ++i;
      ++j;
      continue;
    }
    if (j + 1 < head.size() && working[i] == head[j + 1]) {
      ++j;
      continue;
    }
    if (i + 1 < working.size() && working[i + 1] == head[j]) {
      ++i;
      continue;
    }
    map.working_to_head[i] = static_cast<int>(j);
    map.head_to_working[j] = static_cast<int>(i);
    ++i;
    ++j;
  }
  return map;
}

std::vector<SideBySideDiffRow> build_side_by_side_rows(
    const std::string& unified_diff, const std::vector<std::string>& head_lines,
    const std::vector<std::string>& working_lines) {
  std::vector<SideBySideDiffRow> rows;

  auto append_from_unified = [&](const std::string& diff_output) {
    int old_line = 0;
    int new_line = 0;
    bool in_hunk = false;
    std::string pending_removed;
    int pending_old_line = 0;

    std::istringstream stream(diff_output);
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (line.rfind("@@", 0) == 0) {
        old_line = parse_line_number(line, '-');
        new_line = parse_line_number(line, '+');
        in_hunk = old_line >= 0 && new_line >= 0;
        pending_removed.clear();
        continue;
      }
      if (!in_hunk || line.empty()) {
        continue;
      }
      if (line.rfind("\\ No newline at end of file", 0) == 0) {
        continue;
      }
      const char prefix = line[0];
      if (prefix != ' ' && prefix != '-' && prefix != '+') {
        continue;
      }
      const std::string content = line.size() > 1 ? line.substr(1) : std::string{};
      if (prefix == ' ') {
        pending_removed.clear();
        SideBySideDiffRow row;
        row.kind = SideBySideRowKind::kContext;
        row.left = content;
        row.right = content;
        row.left_line = old_line + 1;
        row.right_line = new_line + 1;
        rows.push_back(std::move(row));
        ++old_line;
        ++new_line;
      } else if (prefix == '-') {
        pending_removed = content;
        pending_old_line = old_line + 1;
        ++old_line;
      } else if (prefix == '+') {
        SideBySideDiffRow row;
        if (!pending_removed.empty()) {
          row.kind = SideBySideRowKind::kModification;
          row.left = pending_removed;
          row.right = content;
          row.left_line = pending_old_line;
          row.right_line = new_line + 1;
          pending_removed.clear();
        } else {
          row.kind = SideBySideRowKind::kAddition;
          row.right = content;
          row.right_line = new_line + 1;
        }
        rows.push_back(std::move(row));
        ++new_line;
      }
    }

    if (!pending_removed.empty()) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kDeletion;
      row.left = pending_removed;
      row.left_line = pending_old_line;
      rows.push_back(std::move(row));
    }
  };

  if (!unified_diff.empty()) {
    append_from_unified(unified_diff);
  }

  if (rows.empty() && head_lines.empty() && !working_lines.empty()) {
    for (std::size_t i = 0; i < working_lines.size(); ++i) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kAddition;
      row.right = working_lines[i];
      row.right_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
    }
    return rows;
  }

  if (rows.empty() && !head_lines.empty() && working_lines.empty()) {
    for (std::size_t i = 0; i < head_lines.size(); ++i) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kDeletion;
      row.left = head_lines[i];
      row.left_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
    }
    return rows;
  }

  if (rows.empty() && head_lines == working_lines) {
    for (std::size_t i = 0; i < working_lines.size(); ++i) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kContext;
      row.left = working_lines[i];
      row.right = working_lines[i];
      row.left_line = static_cast<int>(i) + 1;
      row.right_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
    }
  }

  return rows;
}

std::vector<SideBySideDiffRow> build_side_by_side_rows_from_lines(
    const std::vector<std::string>& head_lines, const std::vector<std::string>& working_lines) {
  std::vector<SideBySideDiffRow> rows;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < working_lines.size() || j < head_lines.size()) {
    if (i < working_lines.size() && j < head_lines.size() &&
        working_lines[i] == head_lines[j]) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kContext;
      row.left = head_lines[j];
      row.right = working_lines[i];
      row.left_line = static_cast<int>(j) + 1;
      row.right_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
      ++i;
      ++j;
      continue;
    }
    if (j < head_lines.size() &&
        (i >= working_lines.size() ||
         (i + 1 < working_lines.size() && working_lines[i + 1] == head_lines[j]))) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kDeletion;
      row.left = head_lines[j];
      row.left_line = static_cast<int>(j) + 1;
      rows.push_back(std::move(row));
      ++j;
      continue;
    }
    if (i < working_lines.size() &&
        (j >= head_lines.size() ||
         (j + 1 < head_lines.size() && working_lines[i] == head_lines[j + 1]))) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kAddition;
      row.right = working_lines[i];
      row.right_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
      ++i;
      continue;
    }
    if (i < working_lines.size() && j < head_lines.size()) {
      SideBySideDiffRow row;
      row.kind = SideBySideRowKind::kModification;
      row.left = head_lines[j];
      row.right = working_lines[i];
      row.left_line = static_cast<int>(j) + 1;
      row.right_line = static_cast<int>(i) + 1;
      rows.push_back(std::move(row));
      ++i;
      ++j;
      continue;
    }
    break;
  }
  return rows;
}

DiffOverviewLines build_diff_overview_lines(const std::vector<SideBySideDiffRow>& rows) {
  DiffOverviewLines out;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    switch (rows[i].kind) {
      case SideBySideRowKind::kAddition:
        out.add_lines.insert(static_cast<int>(i));
        break;
      case SideBySideRowKind::kDeletion:
      case SideBySideRowKind::kModification:
        out.change_lines.insert(static_cast<int>(i));
        break;
      case SideBySideRowKind::kContext:
        break;
    }
  }
  return out;
}

int map_git_scroll_line(const std::vector<int>& line_map, int line) {
  if (line_map.empty()) {
    return std::max(0, line);
  }
  if (line >= 0 && line < static_cast<int>(line_map.size()) && line_map[static_cast<std::size_t>(line)] >= 0) {
    return line_map[static_cast<std::size_t>(line)];
  }
  const int start = std::min(line, static_cast<int>(line_map.size()) - 1);
  for (int i = start; i >= 0; --i) {
    if (line_map[static_cast<std::size_t>(i)] >= 0) {
      return line_map[static_cast<std::size_t>(i)];
    }
  }
  for (int i = std::max(0, line); i < static_cast<int>(line_map.size()); ++i) {
    if (line_map[static_cast<std::size_t>(i)] >= 0) {
      return line_map[static_cast<std::size_t>(i)];
    }
  }
  return 0;
}

namespace {

bool is_change_row(SideBySideRowKind kind) {
  return kind != SideBySideRowKind::kContext;
}

int insert_index_for_head_line(const GitLineMap& map, int head_line_1based) {
  const int head_index = head_line_1based - 1;
  if (head_index < 0) {
    return 0;
  }
  for (int j = head_index - 1; j >= 0; --j) {
    if (j < static_cast<int>(map.head_to_working.size()) && map.head_to_working[static_cast<std::size_t>(j)] >= 0) {
      return map.head_to_working[static_cast<std::size_t>(j)] + 1;
    }
  }
  return 0;
}

struct RevertOp {
  int sort_line = 0;
  enum class Kind { kReplace, kErase, kInsert } kind = Kind::kReplace;
  int index = 0;
  std::string text;
};

}  // namespace

std::vector<GitDiffChangeBlock> build_diff_change_blocks(
    const std::vector<SideBySideDiffRow>& rows) {
  std::vector<GitDiffChangeBlock> blocks;
  int block_start = -1;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (!is_change_row(rows[static_cast<std::size_t>(i)].kind)) {
      if (block_start >= 0) {
        blocks.push_back({block_start, i - 1});
        block_start = -1;
      }
      continue;
    }
    if (block_start < 0) {
      block_start = i;
    }
  }
  if (block_start >= 0) {
    blocks.push_back({block_start, static_cast<int>(rows.size()) - 1});
  }
  return blocks;
}

bool revert_diff_change_block(std::vector<std::string>* working_lines,
                              const std::vector<std::string>& head_lines,
                              const std::vector<SideBySideDiffRow>& rows, int block_index) {
  if (working_lines == nullptr) {
    return false;
  }
  const std::vector<GitDiffChangeBlock> blocks = build_diff_change_blocks(rows);
  if (block_index < 0 || block_index >= static_cast<int>(blocks.size())) {
    return false;
  }
  const GitDiffChangeBlock& block = blocks[static_cast<std::size_t>(block_index)];
  const GitLineMap map = build_git_line_map(head_lines, *working_lines);

  std::vector<RevertOp> ops;
  for (int row_index = block.start_row; row_index <= block.end_row; ++row_index) {
    const SideBySideDiffRow& row = rows[static_cast<std::size_t>(row_index)];
    switch (row.kind) {
      case SideBySideRowKind::kModification:
        if (row.right_line > 0) {
          ops.push_back({row.right_line, RevertOp::Kind::kReplace, row.right_line - 1, row.left});
        }
        break;
      case SideBySideRowKind::kAddition:
        if (row.right_line > 0) {
          ops.push_back({row.right_line, RevertOp::Kind::kErase, row.right_line - 1, {}});
        }
        break;
      case SideBySideRowKind::kDeletion:
        if (row.left_line > 0) {
          ops.push_back({row.left_line, RevertOp::Kind::kInsert,
                         insert_index_for_head_line(map, row.left_line), row.left});
        }
        break;
      case SideBySideRowKind::kContext:
        break;
    }
  }

  std::sort(ops.begin(), ops.end(), [](const RevertOp& a, const RevertOp& b) {
    if (a.sort_line != b.sort_line) {
      return a.sort_line > b.sort_line;
    }
    return static_cast<int>(a.kind) > static_cast<int>(b.kind);
  });

  for (const RevertOp& op : ops) {
    if (op.index < 0) {
      return false;
    }
    switch (op.kind) {
      case RevertOp::Kind::kReplace:
        if (op.index >= static_cast<int>(working_lines->size())) {
          return false;
        }
        (*working_lines)[static_cast<std::size_t>(op.index)] = op.text;
        break;
      case RevertOp::Kind::kErase:
        if (op.index >= static_cast<int>(working_lines->size())) {
          return false;
        }
        working_lines->erase(working_lines->begin() + op.index);
        break;
      case RevertOp::Kind::kInsert:
        if (op.index > static_cast<int>(working_lines->size())) {
          return false;
        }
        working_lines->insert(working_lines->begin() + op.index, op.text);
        break;
    }
  }
  return true;
}

std::vector<std::string> load_lines_from_file(const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return parse_file_lines(content);
}

bool save_lines_to_file(const std::string& path, const std::vector<std::string>& lines) {
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return false;
  }
  bool first = true;
  for (const std::string& line : lines) {
    if (!first) {
      output << '\n';
    }
    first = false;
    output << line;
  }
  return static_cast<bool>(output);
}

}  // namespace tgdb
