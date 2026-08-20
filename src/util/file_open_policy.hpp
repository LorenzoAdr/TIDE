#pragma once

#include <cstdint>
#include <string>

namespace tuide {

enum class FileOpenKind { Allowed, Binary, Large };

struct FileOpenAssessment {
  FileOpenKind kind = FileOpenKind::Allowed;
  std::uintmax_t size_bytes = 0;
};

struct FileOpenPolicy {
  // Default soft warn: opens in read-only virtualized (viewport-only) mode after confirm.
  static constexpr std::uintmax_t kLargeFileBytes = 10ULL * 1024 * 1024;
};

FileOpenAssessment assess_file_open(const std::string& absolute_path,
                                    std::uintmax_t large_file_bytes = FileOpenPolicy::kLargeFileBytes);
bool should_open_as_virtual_text(const std::string& absolute_path,
                                 std::uintmax_t large_file_bytes = FileOpenPolicy::kLargeFileBytes);
std::string format_file_size(std::uintmax_t bytes);

}  // namespace tuide
