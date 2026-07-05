#include "util/compiler_location.hpp"

#include <cctype>
#include <cstring>
#include <filesystem>

namespace tgdb {

namespace {

bool parse_positive_int(std::string_view text, int* out) {
  if (out == nullptr || text.empty()) {
    return false;
  }
  int value = 0;
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return false;
    }
    value = value * 10 + (ch - '0');
    if (value <= 0) {
      return false;
    }
  }
  *out = value;
  return true;
}

bool parse_gcc_style(const std::string& line, std::size_t severity_pos,
                     CompilerLocationMatch* out) {
  if (out == nullptr || severity_pos == 0) {
    return false;
  }
  std::string location = line.substr(0, severity_pos);
  while (!location.empty() && (location.back() == ' ' || location.back() == ':')) {
    location.pop_back();
  }
  if (location.empty()) {
    return false;
  }

  const std::size_t last_sep = location.rfind(':');
  if (last_sep == std::string::npos || last_sep + 1 >= location.size()) {
    return false;
  }

  int tail_number = 0;
  if (!parse_positive_int(std::string_view(location).substr(last_sep + 1), &tail_number)) {
    return false;
  }

  const std::size_t prev_sep = location.rfind(':', last_sep - 1);
  int parsed_line = 0;
  int column = 0;
  std::string path;
  if (prev_sep != std::string::npos) {
    if (!parse_positive_int(std::string_view(location).substr(prev_sep + 1, last_sep - prev_sep - 1),
                            &parsed_line)) {
      return false;
    }
    column = tail_number;
    path = location.substr(0, prev_sep);
  } else {
    parsed_line = tail_number;
    path = location.substr(0, last_sep);
  }

  while (!path.empty() && path.front() == ' ') {
    path.erase(path.begin());
  }
  while (!path.empty() && path.back() == ' ') {
    path.pop_back();
  }
  if (path.empty()) {
    return false;
  }

  out->path = std::move(path);
  out->line = parsed_line;
  out->column = column;
  out->span_start = 0;
  out->span_end = static_cast<int>(location.size());
  return true;
}

std::optional<std::size_t> find_gcc_severity(const std::string& line) {
  static constexpr const char* kMarkers[] = {": fatal error:", ": error:", ": warning:",
                                             ": note:",       ": remark:"};
  std::optional<std::size_t> best;
  for (const char* marker : kMarkers) {
    const std::size_t pos = line.find(marker);
    if (pos != std::string::npos && (!best.has_value() || pos < *best)) {
      best = pos;
    }
  }
  return best;
}

bool parse_msvc_style(const std::string& line, CompilerLocationMatch* out) {
  static constexpr const char* kMarkers[] = {"): error ", "): warning ", "): note ",
                                             "): fatal error "};
  std::size_t marker_pos = std::string::npos;
  std::size_t marker_len = 0;
  for (const char* marker : kMarkers) {
    const std::size_t pos = line.find(marker);
    if (pos != std::string::npos && (marker_pos == std::string::npos || pos < marker_pos)) {
      marker_pos = pos;
      marker_len = ::strlen(marker);
    }
  }
  if (marker_pos == std::string::npos || marker_pos == 0) {
    return false;
  }

  const std::size_t close_paren = marker_pos;
  const std::size_t open_paren = line.rfind('(', close_paren);
  if (open_paren == std::string::npos || open_paren + 1 >= close_paren) {
    return false;
  }

  const std::string inside = line.substr(open_paren + 1, close_paren - open_paren - 1);
  const std::size_t comma = inside.find(',');
  int parsed_line = 0;
  int column = 0;
  if (comma != std::string::npos) {
    if (!parse_positive_int(inside.substr(0, comma), &parsed_line)) {
      return false;
    }
    if (!parse_positive_int(inside.substr(comma + 1), &column)) {
      return false;
    }
  } else if (!parse_positive_int(inside, &parsed_line)) {
    return false;
  }

  std::string path = line.substr(0, open_paren);
  while (!path.empty() && path.front() == ' ') {
    path.erase(path.begin());
  }
  while (!path.empty() && path.back() == ' ') {
    path.pop_back();
  }
  if (path.empty()) {
    return false;
  }

  out->path = std::move(path);
  out->line = parsed_line;
  out->column = column;
  out->span_start = 0;
  out->span_end = static_cast<int>(marker_pos + marker_len - 2);
  return true;
}

bool parse_trailing_location(const std::string& line, std::size_t search_start,
                             CompilerLocationMatch* out) {
  if (out == nullptr) {
    return false;
  }
  std::size_t end = line.size();
  while (end > search_start && (line[end - 1] == ':' || line[end - 1] == ',' ||
                                line[end - 1] == ' ')) {
    --end;
  }
  if (end <= search_start) {
    return false;
  }

  const std::size_t line_sep = line.rfind(':', end - 1);
  if (line_sep == std::string::npos || line_sep < search_start || line_sep + 1 >= end) {
    return false;
  }

  int parsed_line = 0;
  if (!parse_positive_int(std::string_view(line).substr(line_sep + 1, end - line_sep - 1),
                          &parsed_line)) {
    return false;
  }

  std::string path = line.substr(search_start, line_sep - search_start);
  while (!path.empty() && path.front() == ' ') {
    path.erase(path.begin());
  }
  while (!path.empty() && path.back() == ' ') {
    path.pop_back();
  }
  if (path.empty()) {
    return false;
  }
  if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos &&
      path.find('.') == std::string::npos) {
    return false;
  }

  const std::size_t path_pos = line.find(path, search_start);
  out->path = std::move(path);
  out->line = parsed_line;
  out->column = 0;
  out->span_start =
      path_pos == std::string::npos ? static_cast<int>(search_start) : static_cast<int>(path_pos);
  out->span_end = static_cast<int>(end);
  return true;
}

}  // namespace

std::optional<CompilerLocationMatch> find_compiler_location(const std::string& line) {
  if (line.empty()) {
    return std::nullopt;
  }

  CompilerLocationMatch match;
  if (const auto severity = find_gcc_severity(line); severity.has_value()) {
    if (parse_gcc_style(line, *severity, &match)) {
      return match;
    }
  }

  if (parse_msvc_style(line, &match)) {
    return match;
  }

  static constexpr const char* kIncludedFrom = "In file included from ";
  if (const std::size_t prefix = line.find(kIncludedFrom); prefix != std::string::npos) {
    if (parse_trailing_location(line, prefix + ::strlen(kIncludedFrom), &match)) {
      return match;
    }
  }

  if (parse_trailing_location(line, 0, &match)) {
    return match;
  }

  return std::nullopt;
}

std::string resolve_compiler_path(const std::string& path, const std::string& workspace_root,
                                  const std::string& cwd) {
  namespace fs = std::filesystem;
  if (path.empty()) {
    return path;
  }

  std::error_code ec;
  fs::path candidate(path);
  if (candidate.is_absolute()) {
    const fs::path canonical = fs::weakly_canonical(candidate, ec);
    if (!ec) {
      return canonical.string();
    }
    return path;
  }

  auto try_base = [&](const std::string& base) -> std::optional<std::string> {
    if (base.empty()) {
      return std::nullopt;
    }
    ec.clear();
    const fs::path absolute = fs::weakly_canonical(fs::path(base) / candidate, ec);
    if (!ec && fs::exists(absolute)) {
      return absolute.string();
    }
    return std::nullopt;
  };

  if (const auto resolved = try_base(workspace_root); resolved.has_value()) {
    return *resolved;
  }
  if (const auto resolved = try_base(cwd); resolved.has_value()) {
    return *resolved;
  }

  if (!workspace_root.empty()) {
    ec.clear();
    return fs::weakly_canonical(fs::path(workspace_root) / candidate, ec).string();
  }
  if (!cwd.empty()) {
    ec.clear();
    return fs::weakly_canonical(fs::path(cwd) / candidate, ec).string();
  }
  return path;
}

}  // namespace tgdb
