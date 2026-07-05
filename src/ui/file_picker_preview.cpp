#include "ui/file_picker_preview.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#include "util/csv_viewer.hpp"
#include "util/external_viewer.hpp"
#include "util/file_open_policy.hpp"
#include "util/thread_name.hpp"

namespace tgdb {

namespace {

constexpr int kMaxPreviewLines = 200;
constexpr std::size_t kMaxPreviewBytes = 256 * 1024;

bool is_cpp_like_path(const std::string& path) {
  const std::string name = std::filesystem::path(path).filename().string();
  std::string lower = name;
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  static constexpr const char* kExtensions[] = {".c",   ".cc",  ".cpp", ".cxx",
                                                ".h",   ".hh",  ".hpp", ".hxx",
                                                ".inl", ".inc"};
  for (const char* ext : kExtensions) {
    const std::size_t ext_len = std::strlen(ext);
    if (lower.size() >= ext_len &&
        lower.compare(lower.size() - ext_len, ext_len, ext) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

FilePickerPreview::FilePickerPreview() = default;

FilePickerPreview::~FilePickerPreview() {
  reset();
}

void FilePickerPreview::set_notify_callback(NotifyCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  notify_ = std::move(callback);
}

void FilePickerPreview::request(const std::string& absolute_path) {
  join_worker();

  const std::uint64_t request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.request_id = request_id;
    data_.path = absolute_path;
    data_.lines.clear();
    data_.build_file_kind = BuildFileKind::kNone;
    data_.use_cpp_highlight = false;
    data_.unsupported_reason = FilePickerPreviewUnsupportedReason::kNone;
    data_.error_message.clear();
    data_.state = absolute_path.empty() ? FilePickerPreviewState::kIdle
                                        : FilePickerPreviewState::kLoading;
  }

  if (absolute_path.empty()) {
    return;
  }

  NotifyCallback notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    notify = notify_;
  }
  if (notify) {
    notify();
  }

  worker_ = std::thread([this, absolute_path, request_id] {
    set_current_thread_name("file-prev");
    worker_main(std::move(absolute_path), request_id);
  });
}

void FilePickerPreview::reset() {
  next_request_id_.fetch_add(1, std::memory_order_relaxed);
  join_worker();
  std::lock_guard<std::mutex> lock(mutex_);
  data_ = FilePickerPreviewData{};
}

FilePickerPreviewData FilePickerPreview::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return data_;
}

void FilePickerPreview::join_worker() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void FilePickerPreview::worker_main(std::string absolute_path, std::uint64_t request_id) {
  FilePickerPreviewData result;
  result.request_id = request_id;
  result.path = absolute_path;

  if (is_tabular_path(absolute_path)) {
    result.state = FilePickerPreviewState::kUnsupported;
    result.unsupported_reason = FilePickerPreviewUnsupportedReason::kTabular;
    publish(std::move(result), request_id);
    return;
  }
  if (is_pdf_path(absolute_path)) {
    result.state = FilePickerPreviewState::kUnsupported;
    result.unsupported_reason = FilePickerPreviewUnsupportedReason::kPdf;
    publish(std::move(result), request_id);
    return;
  }

  const FileOpenAssessment assessment = assess_file_open(absolute_path);
  if (assessment.kind == FileOpenKind::Binary) {
    result.state = FilePickerPreviewState::kUnsupported;
    result.unsupported_reason = FilePickerPreviewUnsupportedReason::kBinary;
    publish(std::move(result), request_id);
    return;
  }

  std::ifstream input(absolute_path);
  if (!input) {
    result.state = FilePickerPreviewState::kError;
    result.error_message = absolute_path;
    publish(std::move(result), request_id);
    return;
  }

  result.build_file_kind = detect_build_file_kind(absolute_path);
  result.use_cpp_highlight =
      result.build_file_kind == BuildFileKind::kNone && is_cpp_like_path(absolute_path);

  std::size_t bytes_read = 0;
  std::string line;
  while (std::getline(input, line) &&
         result.lines.size() < static_cast<std::size_t>(kMaxPreviewLines)) {
    bytes_read += line.size() + 1;
    if (bytes_read > kMaxPreviewBytes) {
      break;
    }
    result.lines.push_back(std::move(line));
  }
  if (result.lines.empty()) {
    result.lines.push_back("");
  }

  result.state = FilePickerPreviewState::kReady;
  publish(std::move(result), request_id);
}

void FilePickerPreview::publish(FilePickerPreviewData data, std::uint64_t request_id) {
  NotifyCallback notify;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data_.request_id != request_id) {
      return;
    }
    data_ = std::move(data);
    notify = notify_;
  }
  if (notify) {
    notify();
  }
}

}  // namespace tgdb
