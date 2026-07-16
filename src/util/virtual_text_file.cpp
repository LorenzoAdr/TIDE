#include "util/virtual_text_file.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <utility>

#include "i18n/tr.hpp"

namespace tgdb {

namespace {

constexpr int kMaxCachedLines = 1000;
constexpr int kChunkLoadLines = 1000;
constexpr int kPrefetchLines = 100;
constexpr int kInitialCacheLines = 64;
constexpr std::size_t kScanBufferSize = 1024 * 1024;
constexpr std::uint64_t kMaxScanBytesInitial = 8 * 1024 * 1024;
constexpr std::uint64_t kMaxScanBytesChunk = 8 * 1024 * 1024;

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
  loading_more_ = false;
  std::lock_guard<std::mutex> lock(mutex_);
  path_.clear();
  offsets_.clear();
  file_read_pos_ = 0;
  line_count_ = 0;
  has_more_ = false;
  line_cache_.clear();
  cache_start_ = -1;
  cache_end_ = -1;
  state_ = VirtualTextFileState::kIdle;
  error_.clear();
}

void VirtualTextFileStore::open_async(const std::string& path) {
  reset();
  if (path.empty()) {
    return;
  }
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

std::string VirtualTextFileStore::error_message() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

int VirtualTextFileStore::line_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return line_count_;
}

std::string VirtualTextFileStore::read_line_unlocked(int index) const {
  if (index < 0 || index >= line_count_ || path_.empty()) {
    return {};
  }
  const auto cached = line_cache_.find(index);
  if (cached != line_cache_.end()) {
    return cached->second;
  }
  if (index >= static_cast<int>(offsets_.size())) {
    return {};
  }
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    return {};
  }
  return read_line_at(input, offsets_[static_cast<std::size_t>(index)]);
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

void VirtualTextFileStore::open_worker(std::string path) {
  const ChunkScanResult scan =
      scan_chunk_line_offsets(path, 0, kChunkLoadLines, kMaxScanBytesInitial);
  if (scan.offsets.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = VirtualTextFileState::kError;
    error_ = i18n::tr("editor.virtual.open_failed");
    return;
  }

  const int lines = static_cast<int>(scan.offsets.size());
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = VirtualTextFileState::kError;
    error_ = i18n::tr("editor.virtual.open_failed");
    return;
  }

  std::unordered_map<int, std::string> initial_cache;
  const int cache_end = std::min(lines - 1, kInitialCacheLines);
  for (int i = 0; i <= cache_end; ++i) {
    initial_cache[i] = read_line_at(input, scan.offsets[static_cast<std::size_t>(i)]);
  }

  std::lock_guard<std::mutex> lock(mutex_);
  path_ = std::move(path);
  offsets_ = scan.offsets;
  line_count_ = lines;
  has_more_ = !scan.eof;
  file_read_pos_ = scan.read_pos;
  line_cache_ = std::move(initial_cache);
  cache_start_ = 0;
  cache_end_ = cache_end;
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

  const int lines_added = static_cast<int>(scan.offsets.size());
  offsets_.insert(offsets_.end(), scan.offsets.begin(), scan.offsets.end());
  line_count_ += lines_added;
  file_read_pos_ = scan.read_pos;
  has_more_ = !scan.eof;
  loading_more_ = false;
}

bool VirtualTextFileStore::request_load_at_end(int scroll, int visible_rows) {
  if (loading_more_.load()) {
    return false;
  }

  bool should_start = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_more_ || state_ != VirtualTextFileState::kReady || line_count_ <= 0) {
      return false;
    }
    const int total = line_count_;
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
  if (line_count_ <= 0) {
    return;
  }

  int start = std::max(0, scroll - kPrefetchLines);
  int end = std::min(line_count_ - 1, start + kMaxCachedLines - 1);
  if (end - start + 1 < kMaxCachedLines) {
    start = std::max(0, end - kMaxCachedLines + 1);
  }

  if (cache_start_ >= 0 && start >= cache_start_ && end <= cache_end_) {
    return;
  }

  evict_cache_outside_locked(start, end);
  cache_start_ = start;
  cache_end_ = end;
}

void VirtualTextFileStore::ensure_viewport(int scroll, int visible_rows) {
  std::string path;
  std::vector<std::uint64_t> offsets;
  int cache_start = -1;
  int cache_end = -1;
  std::vector<int> rows_to_load;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != VirtualTextFileState::kReady || line_count_ <= 0) {
      return;
    }
    ensure_cache_window_locked(scroll, visible_rows);
    cache_start = cache_start_;
    cache_end = cache_end_;

    for (int i = cache_start; i <= cache_end; ++i) {
      if (line_cache_.find(i) == line_cache_.end()) {
        rows_to_load.push_back(i);
      }
    }
    path = path_;
    offsets = offsets_;
  }

  if (!rows_to_load.empty() && !path.empty()) {
    std::ifstream input(path, std::ios::binary);
    std::vector<std::pair<int, std::string>> loaded;
    loaded.reserve(rows_to_load.size());
    if (input) {
      for (const int row_index : rows_to_load) {
        if (row_index < 0 || row_index >= static_cast<int>(offsets.size())) {
          continue;
        }
        std::string line = read_line_at(input, offsets[static_cast<std::size_t>(row_index)]);
        loaded.emplace_back(row_index, std::move(line));
      }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (path != path_) {
      return;
    }
    for (auto& [row_index, line] : loaded) {
      line_cache_[row_index] = std::move(line);
    }
  }
}

std::string VirtualTextFileStore::line_at(int index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (state_ != VirtualTextFileState::kReady) {
    return {};
  }
  if (index < 0 || index >= line_count_) {
    return {};
  }
  const auto cached = line_cache_.find(index);
  if (cached != line_cache_.end()) {
    return cached->second;
  }
  return read_line_unlocked(index);
}

}  // namespace tgdb
