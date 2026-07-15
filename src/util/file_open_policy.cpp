#include "util/file_open_policy.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "indexer/index_rules.hpp"

namespace fs = std::filesystem;

namespace tgdb {

namespace {

std::string read_binary_sample(const std::string& absolute_path) {
  std::ifstream input(absolute_path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::string sample(8192, '\0');
  input.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  sample.resize(static_cast<std::size_t>(input.gcount()));
  return sample;
}

}  // namespace

FileOpenAssessment assess_file_open(const std::string& absolute_path) {
  FileOpenAssessment result;
  if (absolute_path.empty()) {
    return result;
  }

  std::error_code ec;
  if (!fs::is_regular_file(absolute_path, ec) || ec) {
    return result;
  }

  result.size_bytes = fs::file_size(absolute_path, ec);
  if (ec) {
    return result;
  }

  if (is_probably_binary_path(absolute_path)) {
    result.kind = FileOpenKind::Binary;
    return result;
  }

  const std::string sample = read_binary_sample(absolute_path);
  if (!sample.empty() && text_looks_binary(sample)) {
    result.kind = FileOpenKind::Binary;
    return result;
  }

  if (result.size_bytes > FileOpenPolicy::kMaxOpenFileBytes) {
    result.kind = FileOpenKind::TooLarge;
  } else if (result.size_bytes > FileOpenPolicy::kLargeFileBytes) {
    result.kind = FileOpenKind::Large;
  }
  return result;
}

std::string format_file_size(std::uintmax_t bytes) {
  if (bytes >= 1024 * 1024) {
    const std::uintmax_t mb = (bytes + 512 * 1024) / (1024 * 1024);
    return std::to_string(mb) + " MB";
  }
  if (bytes >= 1024) {
    const std::uintmax_t kb = (bytes + 512) / 1024;
    return std::to_string(kb) + " KB";
  }
  return std::to_string(bytes) + " B";
}

}  // namespace tgdb
