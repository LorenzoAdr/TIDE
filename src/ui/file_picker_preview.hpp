#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "util/build_file_highlight.hpp"

namespace tgdb {

enum class FilePickerPreviewState {
  kIdle,
  kLoading,
  kReady,
  kUnsupported,
  kError,
};

enum class FilePickerPreviewUnsupportedReason {
  kNone,
  kBinary,
  kTabular,
  kPdf,
};

struct FilePickerPreviewData {
  std::uint64_t request_id = 0;
  std::string path;
  std::vector<std::string> lines;
  BuildFileKind build_file_kind = BuildFileKind::kNone;
  bool use_cpp_highlight = false;
  FilePickerPreviewState state = FilePickerPreviewState::kIdle;
  FilePickerPreviewUnsupportedReason unsupported_reason =
      FilePickerPreviewUnsupportedReason::kNone;
  std::string error_message;
};

class FilePickerPreview {
 public:
  using NotifyCallback = std::function<void()>;

  FilePickerPreview();
  ~FilePickerPreview();

  FilePickerPreview(const FilePickerPreview&) = delete;
  FilePickerPreview& operator=(const FilePickerPreview&) = delete;

  void set_notify_callback(NotifyCallback callback);
  void request(const std::string& absolute_path);
  void reset();

  FilePickerPreviewData snapshot() const;

 private:
  void join_worker();
  void worker_main(std::string absolute_path, std::uint64_t request_id);
  void publish(FilePickerPreviewData data, std::uint64_t request_id);

  mutable std::mutex mutex_;
  NotifyCallback notify_;
  std::thread worker_;
  std::atomic<std::uint64_t> next_request_id_{1};
  FilePickerPreviewData data_;
};

}  // namespace tgdb
