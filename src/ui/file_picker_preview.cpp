#include "ui/file_picker_preview.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

#include "util/csv_viewer.hpp"
#include "util/external_viewer.hpp"
#include "util/file_open_policy.hpp"
#include "util/thread_name.hpp"
#include "indexer/index_rules.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_service.hpp"

namespace tgdb {

namespace {

constexpr int kMaxPreviewLines = 200;
constexpr std::size_t kMaxPreviewBytes = 256 * 1024;
constexpr int kContextBeforeLine = 4;

void fill_preview_display_lines(FilePickerPreviewData* result, int center_line) {
  if (result == nullptr || result->highlight_lines.empty()) {
    return;
  }
  const int start_line =
      center_line > 0 ? std::max(1, center_line - kContextBeforeLine) : 1;
  result->first_line_number = start_line;
  result->lines.clear();
  const int start_index = start_line - 1;
  const int end_index = std::min(
      static_cast<int>(result->highlight_lines.size()),
      start_index + kMaxPreviewLines);
  for (int i = start_index; i < end_index; ++i) {
    result->lines.push_back(result->highlight_lines[static_cast<std::size_t>(i)]);
  }
  if (result->lines.empty()) {
    result->lines.push_back("");
  }
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

void FilePickerPreview::request(const std::string& absolute_path, int center_line) {
  join_worker();

  const std::uint64_t request_id = next_request_id_.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.request_id = request_id;
    data_.path = absolute_path;
    data_.lines.clear();
    data_.first_line_number = 1;
    data_.highlight_line = center_line > 0 ? center_line : 0;
    data_.highlight_lines.clear();
    data_.build_file_kind = BuildFileKind::kNone;
    data_.use_tree_sitter = false;
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

  worker_ = std::thread([this, absolute_path, center_line, request_id] {
    set_current_thread_name("file-prev");
    worker_main(std::move(absolute_path), center_line, request_id);
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

void FilePickerPreview::worker_main(std::string absolute_path, int center_line,
                                    std::uint64_t request_id) {
  FilePickerPreviewData result;
  result.request_id = request_id;
  result.path = absolute_path;
  result.highlight_line = center_line > 0 ? center_line : 0;

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
  result.use_tree_sitter =
      result.build_file_kind == BuildFileKind::kNone && is_indexed_source_path(absolute_path);

  std::size_t bytes_read = 0;
  std::string line;
  while (std::getline(input, line)) {
    bytes_read += line.size() + 1;
    if (bytes_read > kMaxPreviewBytes) {
      break;
    }
    result.highlight_lines.push_back(std::move(line));
  }
  if (result.highlight_lines.empty()) {
    result.highlight_lines.push_back("");
  }

  if (result.use_tree_sitter) {
    tree_sitter_service().prepare_document(absolute_path,
                                           join_editor_lines(result.highlight_lines));
  }

  fill_preview_display_lines(&result, center_line);

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
