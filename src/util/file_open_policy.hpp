#pragma once

#include <cstdint>
#include <string>

namespace tgdb {

enum class FileOpenKind { Allowed, Binary, Large };

struct FileOpenAssessment {
  FileOpenKind kind = FileOpenKind::Allowed;
  std::uintmax_t size_bytes = 0;
};

struct FileOpenPolicy {
  static constexpr std::uintmax_t kLargeFileBytes = 1024 * 1024;
};

FileOpenAssessment assess_file_open(const std::string& absolute_path);
std::string format_file_size(std::uintmax_t bytes);

}  // namespace tgdb
