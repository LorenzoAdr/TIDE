#include "util/virtual_text_file.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <utility>

#include "i18n/tr.hpp"

namespace tuide {

namespace {

constexpr int kMaxCachedLines = 1000;
constexpr int kChunkLoadLines = 1000;
constexpr int kPrefetchLines = 100;
constexpr int kInitialCacheLines = 64;
constexpr std::size_t kScanBufferSize = 1024 * 1024;
constexpr std::uint64_t kMaxScanBytesInitial = 8 * 1024 * 1024;
constexpr std::uint64_t kMaxScanBytesChunk = 8 * 1024 * 1024;
constexpr std::size_t kMaxUndoEntries = 256;

std::string normalize_line(std::string line) {
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

std::string read_line_at(std::ifstream& input, std::uint64_t offset) {
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset));
  std::string line;
  if (!std::getline(input, line)) {
    return {};
  }
  return normalize_line(std::move(line));
}

struct ChunkScanResult {
  std::vector<std::uint64_t> offsets;
  std::uint64_t read_pos = 0;
  bool eof = true;
};

ChunkScanResult scan_chunk_line_offsets(const std::string& path, std::uint64_t start_pos,
                                        int max_lines, std::uint64_t max_scan_bytes = 0) {
  ChunkScanResult result;
  if (max_lines <= 0) {
    return result;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return result;
  }
  input.seekg(static_cast<std::streamoff>(start_pos));

  result.offsets.push_back(start_pos);
  if (max_lines == 1) {
    result.read_pos = start_pos;
    result.eof = input.peek() == std::ifstream::traits_type::eof();
    return result;
  }

  std::vector<char> buffer(kScanBufferSize);
  std::uint64_t base_pos = start_pos;
  std::uint64_t bytes_scanned = 0;
  int lines_found = 1;

  while (lines_found < max_lines && input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::size_t bytes_read = static_cast<std::size_t>(input.gcount());
    if (bytes_read == 0) {
      result.eof = true;
      result.read_pos = base_pos;
      break;
    }
    bytes_scanned += bytes_read;

    std::size_t scan_at = 0;
    while (scan_at < bytes_read && lines_found < max_lines) {
      const char* chunk = buffer.data() + scan_at;
      const std::size_t remaining = bytes_read - scan_at;

      const void* nl_found = std::memchr(chunk, '\n', remaining);
      const void* cr_found = std::memchr(chunk, '\r', remaining);

      std::size_t break_rel = remaining;
      bool has_break = false;
      if (nl_found != nullptr) {
        break_rel = static_cast<const char*>(nl_found) - chunk;
        has_break = true;
      }
      if (cr_found != nullptr) {
        const std::size_t cr_rel = static_cast<const char*>(cr_found) - chunk;
        if (!has_break || cr_rel < break_rel) {
          break_rel = cr_rel;
          has_break = true;
        }
      }
      if (!has_break) {
        break;
      }

      std::size_t advance = 1;
      if (chunk[break_rel] == '\r' && break_rel + 1 < remaining && chunk[break_rel + 1] == '\n') {
        advance = 2;
      }

      result.offsets.push_back(base_pos + scan_at + break_rel + advance);
      ++lines_found;
      scan_at += break_rel + advance;
    }

    base_pos += bytes_read;

    if (max_scan_bytes > 0 && bytes_scanned >= max_scan_bytes && lines_found < max_lines) {
      result.read_pos = base_pos;
      result.eof = input.peek() == std::ifstream::traits_type::eof();
      break;
    }

    if (lines_found >= max_lines) {
      result.read_pos = result.offsets.back();
      input.clear();
      const std::streamoff current = input.tellg();
      result.eof = !input || (input.peek() == std::ifstream::traits_type::eof());
      if (input && current >= 0) {
        result.read_pos = static_cast<std::uint64_t>(current);
      }
      break;
    }
  }

  if (lines_found < max_lines && result.read_pos == 0) {
    result.eof = true;
    result.read_pos = base_pos;
  }
  return result;
}

int utf8_prev_col(const std::string& s, int col) {
  if (col <= 0) {
    return 0;
  }
  int i = std::min(col, static_cast<int>(s.size()));
  --i;
  while (i > 0 && (static_cast<unsigned char>(s[static_cast<std::size_t>(i)]) & 0xC0) == 0x80) {
    --i;
  }
  return i;
}

int utf8_next_col(const std::string& s, int col) {
  if (col < 0) {
    return 0;
  }
  if (col >= static_cast<int>(s.size())) {
    return static_cast<int>(s.size());
  }
  int i = col + 1;
  while (i < static_cast<int>(s.size()) &&
         (static_cast<unsigned char>(s[static_cast<std::size_t>(i)]) & 0xC0) == 0x80) {
    ++i;
  }
  return i;
}

}  // namespace

VirtualTextFileStore::VirtualTextFileStore() = default;

VirtualTextFileStore::~VirtualTextFileStore() { reset(); }

void VirtualTextFileStore::reset() {
  if (worker_.joinable()) {
    worker_.join();
  }
  if (load_more_thread_.joinable()) {
    load_more_thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  path_.clear();
  lines_.clear();
  file_read_pos_ = 0;
  has_more_ = false;
  dirty_ = false;
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  state_ = VirtualTextFileState::kIdle;
  error_.clear();
  undo_.clear();
  redo_.clear();
  loading_more_ = false;
}

void VirtualTextFileStore::open_async(const std::string& path) {
  reset();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    state_ = VirtualTextFileState::kIndexing;
  }
  worker_ = std::thread([this, path] { open_worker(path); });
}

VirtualTextFileState VirtualTextFileStore::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool VirtualTextFileStore::ready() const { return state() == VirtualTextFileState::kReady; }

bool VirtualTextFileStore::indexing() const {
  return state() == VirtualTextFileState::kIndexing;
}

bool VirtualTextFileStore::loading_more() const { return loading_more_.load(); }

bool VirtualTextFileStore::has_more() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_more_;
}

bool VirtualTextFileStore::dirty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dirty_;
}

std::string VirtualTextFileStore::error_message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

std::string VirtualTextFileStore::path() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return path_;
}

int VirtualTextFileStore::line_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(lines_.size());
}

void VirtualTextFileStore::clear_dirty() {
  std::lock_guard<std::mutex> lock(mutex_);
  dirty_ = false;
}

void VirtualTextFileStore::mark_dirty_unlocked() { dirty_ = true; }

void VirtualTextFileStore::push_undo_unlocked(UndoEntry entry) {
  undo_.push_back(std::move(entry));
  if (undo_.size() > kMaxUndoEntries) {
    undo_.erase(undo_.begin());
  }
  redo_.clear();
}

std::string VirtualTextFileStore::read_disk_line_unlocked(int index) const {
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return {};
  }
  const LogicalLine& line = lines_[static_cast<std::size_t>(index)];
  if (!line.from_disk) {
    return {};
  }
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return {};
  }
  return read_line_at(input, line.disk_offset);
}

std::string VirtualTextFileStore::line_content_unlocked(int index) const {
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return {};
  }
  const LogicalLine& line = lines_[static_cast<std::size_t>(index)];
  if (line.edited.has_value()) {
    return *line.edited;
  }
  const auto cached = line_cache_.find(index);
  if (cached != line_cache_.end()) {
    return cached->second;
  }
  return read_disk_line_unlocked(index);
}

void VirtualTextFileStore::ensure_editable_unlocked(int index) {
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return;
  }
  LogicalLine& line = lines_[static_cast<std::size_t>(index)];
  if (!line.edited.has_value()) {
    line.edited = line_content_unlocked(index);
  }
}

void VirtualTextFileStore::evict_cache_outside_locked(int start, int end) {
  for (auto it = line_cache_.begin(); it != line_cache_.end();) {
    if (it->first < start || it->first > end) {
      it = line_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

bool VirtualTextFileStore::append_chunk_unlocked(const std::vector<std::uint64_t>& offsets,
                                                std::uint64_t read_pos, bool eof) {
  if (offsets.empty()) {
    has_more_ = false;
    file_read_pos_ = read_pos;
    return false;
  }
  lines_.reserve(lines_.size() + offsets.size());
  for (std::uint64_t offset : offsets) {
    LogicalLine line;
    line.disk_offset = offset;
    line.from_disk = true;
    lines_.push_back(std::move(line));
  }
  file_read_pos_ = read_pos;
  has_more_ = !eof;
  return true;
}

void VirtualTextFileStore::open_worker(std::string path) {
  const ChunkScanResult scan =
      scan_chunk_line_offsets(path, 0, kChunkLoadLines, kMaxScanBytesInitial);
  if (scan.offsets.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = VirtualTextFileState::kError;
    error_ = i18n::tr("editor.virtual.open_failed");
    return;
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = VirtualTextFileState::kError;
    error_ = i18n::tr("editor.virtual.open_failed");
    return;
  }

  std::unordered_map<int, std::string> initial_cache;
  const int cache_end =
      std::min(static_cast<int>(scan.offsets.size()) - 1, kInitialCacheLines);
  for (int i = 0; i <= cache_end; ++i) {
    initial_cache[i] = read_line_at(input, scan.offsets[static_cast<std::size_t>(i)]);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  path_ = std::move(path);
  lines_.clear();
  append_chunk_unlocked(scan.offsets, scan.read_pos, scan.eof);
  line_cache_ = std::move(initial_cache);
  cache_start_ = 0;
  cache_end_ = cache_end;
  undo_.clear();
  redo_.clear();
  dirty_ = false;
  state_ = VirtualTextFileState::kReady;
}

void VirtualTextFileStore::load_more_worker() {
  std::string path;
  std::uint64_t read_pos = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_more_ || path_.empty()) {
      loading_more_ = false;
      return;
    }
    path = path_;
    read_pos = file_read_pos_;
  }

  const ChunkScanResult scan =
      scan_chunk_line_offsets(path, read_pos, kChunkLoadLines, kMaxScanBytesChunk);
  if (scan.offsets.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    has_more_ = false;
    loading_more_ = false;
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (path != path_) {
    loading_more_ = false;
    return;
  }
  append_chunk_unlocked(scan.offsets, scan.read_pos, scan.eof);
  loading_more_ = false;
}

bool VirtualTextFileStore::request_load_at_end(int scroll, int visible_rows) {
  if (loading_more_.load()) {
    return false;
  }

  bool should_start = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_more_ || state_ != VirtualTextFileState::kReady || lines_.empty()) {
      return false;
    }
    const int total = static_cast<int>(lines_.size());
    const int viewport_end = scroll + std::max(1, visible_rows);
    const int trigger_at = std::max(0, total - kPrefetchLines);
    if (viewport_end < trigger_at) {
      return false;
    }
    loading_more_ = true;
    should_start = true;
  }

  if (!should_start) {
    return false;
  }
  if (load_more_thread_.joinable()) {
    load_more_thread_.join();
  }
  load_more_thread_ = std::thread([this] { load_more_worker(); });
  return true;
}

void VirtualTextFileStore::ensure_cache_window_locked(int scroll, int visible_rows) {
  if (lines_.empty()) {
    return;
  }

  int start = std::max(0, scroll - kPrefetchLines);
  int end = std::min(static_cast<int>(lines_.size()) - 1, start + kMaxCachedLines - 1);
  if (end - start + 1 < kMaxCachedLines) {
    start = std::max(0, end - kMaxCachedLines + 1);
  }
  if (start == cache_start_ && end == cache_end_) {
    return;
  }

  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return;
  }
  for (int i = start; i <= end; ++i) {
    if (line_cache_.find(i) != line_cache_.end()) {
      continue;
    }
    const LogicalLine& line = lines_[static_cast<std::size_t>(i)];
    if (line.edited.has_value()) {
      line_cache_[i] = *line.edited;
    } else if (line.from_disk) {
      line_cache_[i] = read_line_at(input, line.disk_offset);
    } else {
      line_cache_[i] = {};
    }
  }
  cache_start_ = start;
  cache_end_ = end;
  evict_cache_outside_locked(start, end);
}

void VirtualTextFileStore::ensure_viewport(int scroll, int visible_rows) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || lines_.empty()) {
    return;
  }
  ensure_cache_window_locked(scroll, visible_rows);
}

std::string VirtualTextFileStore::line_at(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady) {
    return {};
  }
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return {};
  }
  const LogicalLine& line = lines_[static_cast<std::size_t>(index)];
  if (line.edited.has_value()) {
    return *line.edited;
  }
  const auto cached = line_cache_.find(index);
  if (cached != line_cache_.end()) {
    return cached->second;
  }
  std::string text = read_disk_line_unlocked(index);
  line_cache_[index] = text;
  return text;
}

bool VirtualTextFileStore::finish_indexing() {
  for (;;) {
    std::string path;
    std::uint64_t read_pos = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (state_ != VirtualTextFileState::kReady) {
        return false;
      }
      if (!has_more_) {
        return true;
      }
      path = path_;
      read_pos = file_read_pos_;
    }
    if (loading_more_.load() && load_more_thread_.joinable()) {
      load_more_thread_.join();
      loading_more_ = false;
      continue;
    }
    const ChunkScanResult scan =
        scan_chunk_line_offsets(path, read_pos, kChunkLoadLines, kMaxScanBytesChunk);
    std::lock_guard<std::mutex> lock(mutex_);
    if (path != path_) {
      return false;
    }
    if (scan.offsets.empty()) {
      has_more_ = false;
      return true;
    }
    append_chunk_unlocked(scan.offsets, scan.read_pos, scan.eof);
  }
}

void VirtualTextFileStore::set_line(int index, std::string text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || index < 0 ||
      index >= static_cast<int>(lines_.size())) {
    return;
  }
  ensure_editable_unlocked(index);
  UndoEntry entry;
  entry.kind = UndoKind::Set;
  entry.index = index;
  entry.before = lines_[static_cast<std::size_t>(index)].edited.value_or(std::string{});
  entry.after = text;
  push_undo_unlocked(std::move(entry));
  lines_[static_cast<std::size_t>(index)].edited = std::move(text);
  line_cache_[index] = *lines_[static_cast<std::size_t>(index)].edited;
  mark_dirty_unlocked();
}

void VirtualTextFileStore::insert_line(int index, std::string text) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady) {
    return;
  }
  const int count = static_cast<int>(lines_.size());
  if (index < 0 || index > count) {
    return;
  }
  LogicalLine line;
  line.edited = std::move(text);
  UndoEntry entry;
  entry.kind = UndoKind::Insert;
  entry.index = index;
  entry.after = *line.edited;
  push_undo_unlocked(std::move(entry));
  lines_.insert(lines_.begin() + index, std::move(line));
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  mark_dirty_unlocked();
}

void VirtualTextFileStore::erase_line(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || lines_.size() <= 1) {
    return;
  }
  if (index < 0 || index >= static_cast<int>(lines_.size())) {
    return;
  }
  UndoEntry entry;
  entry.kind = UndoKind::Erase;
  entry.index = index;
  entry.erased = lines_[static_cast<std::size_t>(index)];
  if (!entry.erased.edited.has_value()) {
    entry.erased.edited = line_content_unlocked(index);
  }
  push_undo_unlocked(std::move(entry));
  lines_.erase(lines_.begin() + index);
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  mark_dirty_unlocked();
}

bool VirtualTextFileStore::insert_utf8(int line, int col, const std::string& utf8) {
  if (utf8.empty()) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || line < 0 ||
      line >= static_cast<int>(lines_.size())) {
    return false;
  }
  ensure_editable_unlocked(line);
  std::string& text = *lines_[static_cast<std::size_t>(line)].edited;
  col = std::max(0, std::min(col, static_cast<int>(text.size())));
  UndoEntry entry;
  entry.kind = UndoKind::Set;
  entry.index = line;
  entry.before = text;
  text.insert(static_cast<std::size_t>(col), utf8);
  entry.after = text;
  push_undo_unlocked(std::move(entry));
  line_cache_[line] = text;
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::backspace_at(int* line, int* col) {
  if (line == nullptr || col == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || *line < 0 ||
      *line >= static_cast<int>(lines_.size())) {
    return false;
  }
  ensure_editable_unlocked(*line);
  std::string& text = *lines_[static_cast<std::size_t>(*line)].edited;
  if (*col > 0) {
    const int prev = utf8_prev_col(text, *col);
    UndoEntry entry;
    entry.kind = UndoKind::Set;
    entry.index = *line;
    entry.before = text;
    text.erase(static_cast<std::size_t>(prev), static_cast<std::size_t>(*col - prev));
    entry.after = text;
    push_undo_unlocked(std::move(entry));
    *col = prev;
    line_cache_[*line] = text;
    mark_dirty_unlocked();
    return true;
  }
  if (*line == 0) {
    return false;
  }
  ensure_editable_unlocked(*line - 1);
  std::string& prev_text = *lines_[static_cast<std::size_t>(*line - 1)].edited;
  const int join_col = static_cast<int>(prev_text.size());
  UndoEntry erase_entry;
  erase_entry.kind = UndoKind::Erase;
  erase_entry.index = *line;
  erase_entry.erased = lines_[static_cast<std::size_t>(*line)];
  if (!erase_entry.erased.edited.has_value()) {
    erase_entry.erased.edited = text;
  }
  UndoEntry set_entry;
  set_entry.kind = UndoKind::Set;
  set_entry.index = *line - 1;
  set_entry.before = prev_text;
  prev_text += text;
  set_entry.after = prev_text;
  // Store as two undo steps (reverse order on undo stack: set then erase means
  // undo erase first then set — push erase last so undo restores line then text).
  push_undo_unlocked(std::move(set_entry));
  push_undo_unlocked(std::move(erase_entry));
  lines_.erase(lines_.begin() + *line);
  --(*line);
  *col = join_col;
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::delete_at(int* line, int* col) {
  if (line == nullptr || col == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || *line < 0 ||
      *line >= static_cast<int>(lines_.size())) {
    return false;
  }
  ensure_editable_unlocked(*line);
  std::string& text = *lines_[static_cast<std::size_t>(*line)].edited;
  if (*col < static_cast<int>(text.size())) {
    const int next = utf8_next_col(text, *col);
    UndoEntry entry;
    entry.kind = UndoKind::Set;
    entry.index = *line;
    entry.before = text;
    text.erase(static_cast<std::size_t>(*col), static_cast<std::size_t>(next - *col));
    entry.after = text;
    push_undo_unlocked(std::move(entry));
    line_cache_[*line] = text;
    mark_dirty_unlocked();
    return true;
  }
  if (*line + 1 >= static_cast<int>(lines_.size())) {
    return false;
  }
  ensure_editable_unlocked(*line + 1);
  const std::string next_text = *lines_[static_cast<std::size_t>(*line + 1)].edited;
  UndoEntry set_entry;
  set_entry.kind = UndoKind::Set;
  set_entry.index = *line;
  set_entry.before = text;
  text += next_text;
  set_entry.after = text;
  UndoEntry erase_entry;
  erase_entry.kind = UndoKind::Erase;
  erase_entry.index = *line + 1;
  erase_entry.erased = lines_[static_cast<std::size_t>(*line + 1)];
  if (!erase_entry.erased.edited.has_value()) {
    erase_entry.erased.edited = next_text;
  }
  push_undo_unlocked(std::move(set_entry));
  push_undo_unlocked(std::move(erase_entry));
  lines_.erase(lines_.begin() + (*line + 1));
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::insert_newline(int* line, int* col) {
  if (line == nullptr || col == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady || *line < 0 ||
      *line >= static_cast<int>(lines_.size())) {
    return false;
  }
  ensure_editable_unlocked(*line);
  std::string& text = *lines_[static_cast<std::size_t>(*line)].edited;
  *col = std::max(0, std::min(*col, static_cast<int>(text.size())));
  const std::string right = text.substr(static_cast<std::size_t>(*col));
  UndoEntry set_entry;
  set_entry.kind = UndoKind::Set;
  set_entry.index = *line;
  set_entry.before = text;
  text.resize(static_cast<std::size_t>(*col));
  set_entry.after = text;
  LogicalLine inserted;
  inserted.edited = right;
  UndoEntry insert_entry;
  insert_entry.kind = UndoKind::Insert;
  insert_entry.index = *line + 1;
  insert_entry.after = right;
  push_undo_unlocked(std::move(set_entry));
  push_undo_unlocked(std::move(insert_entry));
  lines_.insert(lines_.begin() + (*line + 1), std::move(inserted));
  ++(*line);
  *col = 0;
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::undo(int* line, int* col) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (undo_.empty()) {
    return false;
  }
  UndoEntry entry = std::move(undo_.back());
  undo_.pop_back();
  switch (entry.kind) {
    case UndoKind::Set:
      if (entry.index >= 0 && entry.index < static_cast<int>(lines_.size())) {
        lines_[static_cast<std::size_t>(entry.index)].edited = entry.before;
        line_cache_[entry.index] = entry.before;
        if (line != nullptr) {
          *line = entry.index;
        }
        if (col != nullptr) {
          *col = std::min(static_cast<int>(entry.before.size()),
                          col != nullptr ? *col : static_cast<int>(entry.before.size()));
        }
      }
      break;
    case UndoKind::Insert:
      if (entry.index >= 0 && entry.index < static_cast<int>(lines_.size())) {
        lines_.erase(lines_.begin() + entry.index);
        line_cache_.clear();
        cache_start_ = -1;
        cache_end_ = -1;
        if (line != nullptr) {
          *line = std::max(0, entry.index - 1);
        }
        if (col != nullptr) {
          *col = 0;
        }
      }
      break;
    case UndoKind::Erase:
      if (entry.index >= 0 && entry.index <= static_cast<int>(lines_.size())) {
        lines_.insert(lines_.begin() + entry.index, entry.erased);
        line_cache_.clear();
        cache_start_ = -1;
        cache_end_ = -1;
        if (line != nullptr) {
          *line = entry.index;
        }
        if (col != nullptr) {
          *col = 0;
        }
      }
      break;
  }
  redo_.push_back(std::move(entry));
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::redo(int* line, int* col) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (redo_.empty()) {
    return false;
  }
  UndoEntry entry = std::move(redo_.back());
  redo_.pop_back();
  switch (entry.kind) {
    case UndoKind::Set:
      if (entry.index >= 0 && entry.index < static_cast<int>(lines_.size())) {
        lines_[static_cast<std::size_t>(entry.index)].edited = entry.after;
        line_cache_[entry.index] = entry.after;
        if (line != nullptr) {
          *line = entry.index;
        }
        if (col != nullptr) {
          *col = static_cast<int>(entry.after.size());
        }
      }
      break;
    case UndoKind::Insert: {
      LogicalLine line_slot;
      line_slot.edited = entry.after;
      if (entry.index >= 0 && entry.index <= static_cast<int>(lines_.size())) {
        lines_.insert(lines_.begin() + entry.index, std::move(line_slot));
        line_cache_.clear();
        cache_start_ = -1;
        cache_end_ = -1;
        if (line != nullptr) {
          *line = entry.index;
        }
        if (col != nullptr) {
          *col = 0;
        }
      }
      break;
    }
    case UndoKind::Erase:
      if (entry.index >= 0 && entry.index < static_cast<int>(lines_.size())) {
        lines_.erase(lines_.begin() + entry.index);
        line_cache_.clear();
        cache_start_ = -1;
        cache_end_ = -1;
        if (line != nullptr) {
          *line = std::min(entry.index, static_cast<int>(lines_.size()) - 1);
        }
        if (col != nullptr) {
          *col = 0;
        }
      }
      break;
  }
  undo_.push_back(std::move(entry));
  mark_dirty_unlocked();
  return true;
}

bool VirtualTextFileStore::save(const std::string& absolute_path) {
  if (!finish_indexing()) {
    return false;
  }

  std::vector<std::string> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != VirtualTextFileState::kReady) {
      return false;
    }
    snapshot.reserve(lines_.size());
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
      snapshot.push_back(line_content_unlocked(i));
    }
  }

  const std::string tmp = absolute_path + ".tuide-tmp";
  {
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    for (std::size_t i = 0; i < snapshot.size(); ++i) {
      output << snapshot[i];
      if (i + 1 < snapshot.size()) {
        output << '\n';
      }
    }
    if (!output.flush()) {
      return false;
    }
  }
  if (std::rename(tmp.c_str(), absolute_path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }

  // Re-open from saved file so disk offsets stay coherent.
  open_async(absolute_path);
  if (worker_.joinable()) {
    worker_.join();
  }
  clear_dirty();
  return ready();
}

}  // namespace tuide
