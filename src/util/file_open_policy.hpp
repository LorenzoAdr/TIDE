#pragma once

#include <cstdint>
#include <string>

namespace tgdb {

enum class FileOpenKind { Allowed, Binary, Large, TooLarge };

struct FileOpenAssessment {
  FileOpenKind kind = FileOpenKind::Allowed;
  std::uintmax_t size_bytes = 0;
};

struct FileOpenPolicy {
  // Soft warn: user can confirm after a modal.
  static constexpr std::uintmax_t kLargeFileBytes = 1024 * 1024;
  // Hard reject: opening larger files can OOM the process (silent SIGKILL).
  static constexpr std::uintmax_t kMaxOpenFileBytes = 16ULL * 1024 * 1024;
};

FileOpenAssessment assess_file_open(const std::string& absolute_path);
std::string format_file_size(std::uintmax_t bytes);

}  // namespace tgdb
