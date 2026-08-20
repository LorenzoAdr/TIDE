#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace tuide {

enum class VirtualTextFileState { kIdle, kIndexing, kReady, kError };

// Viewport-backed text file with optional sparse edits (memory overlays).
// Unedited lines stay on disk; save streams the logical document.
class VirtualTextFileStore {
 public:
  VirtualTextFileStore();
  ~VirtualTextFileStore();

  void open_async(const std::string& path);
  void reset();

  VirtualTextFileState state() const;
  bool ready() const;
  bool indexing() const;
  bool loading_more() const;
  bool has_more() const;
  bool dirty() const;
  std::string error_message() const;
  std::string path() const;

  int line_count() const;
  bool request_load_at_end(int scroll, int visible_rows);
  std::string line_at(int index);
  void ensure_viewport(int scroll, int visible_rows);

  // Finish indexing remaining chunks synchronously (needed before full save).
  bool finish_indexing();

  void set_line(int index, std::string text);
  void insert_line(int index, std::string text);
  void erase_line(int index);

  // Character-level helpers (single caret). Return false if out of range / not ready.
  bool insert_utf8(int line, int col, const std::string& utf8);
  bool backspace_at(int* line, int* col);
  bool delete_at(int* line, int* col);
  bool insert_newline(int* line, int* col);
  bool undo(int* line, int* col);
  bool redo(int* line, int* col);

  // Rewrite path from logical lines. Indexes any remaining tail first.
  bool save(const std::string& absolute_path);
  void clear_dirty();

 private:
  struct LogicalLine {
    std::uint64_t disk_offset = 0;
    bool from_disk = false;
    std::optional<std::string> edited;
  };

  enum class UndoKind { Set, Insert, Erase };
  struct UndoEntry {
    UndoKind kind = UndoKind::Set;
    int index = 0;
    std::string before;
    std::string after;
    LogicalLine erased;
  };

  void open_worker(std::string path);
  void load_more_worker();
  std::string read_disk_line_unlocked(int index) const;
  std::string line_content_unlocked(int index) const;
  void ensure_editable_unlocked(int index);
  void ensure_cache_window_locked(int scroll, int visible_rows);
  void evict_cache_outside_locked(int start, int end);
  void push_undo_unlocked(UndoEntry entry);
  void mark_dirty_unlocked();
  bool append_chunk_unlocked(const std::vector<std::uint64_t>& offsets, std::uint64_t read_pos,
                             bool eof);

  mutable std::mutex mutex_;
  std::thread worker_;
  std::thread load_more_thread_;
  std::atomic<bool> loading_more_{false};
  std::string path_;
  std::vector<LogicalLine> lines_;
  std::uint64_t file_read_pos_ = 0;
  bool has_more_ = false;
  bool dirty_ = false;
  std::unordered_map<int, std::string> line_cache_;
  int cache_start_ = -1;
  int cache_end_ = -1;
  VirtualTextFileState state_ = VirtualTextFileState::kIdle;
  std::string error_;
  std::vector<UndoEntry> undo_;
  std::vector<UndoEntry> redo_;
};

}  // namespace tuide
