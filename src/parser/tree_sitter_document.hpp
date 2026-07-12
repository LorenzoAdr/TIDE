#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <tree_sitter/api.h>
}

#include "editor/editor_buffer_source.hpp"
#include "editor/editor_text.hpp"
#include "symbols/symbol_provider.hpp"
#include "util/line_source.hpp"

namespace tgdb {

constexpr int kTreeSitterParseDebounceMs = 120;

struct HighlightSpan {
  int start_col = 0;
  int end_col = 0;
  std::string capture;
};

struct LineHighlights {
  std::vector<HighlightSpan> spans;
};

// Sync tree-sitter colors for a visible viewport slice while the async worker
// is still building full-document highlights (typically on file open).
struct ViewportHighlightPreview {
  std::string source;
  int first_line = 0;
  int last_line = -1;
  std::unordered_map<int, LineHighlights> by_line;
};

std::string join_editor_lines(const std::vector<std::string>& lines);
// EditorText already maintains this exact join incrementally where possible
// (see EditorBuffer::joined_source_cache) -- to_string() here is the plain,
// non-incremental O(n) fallback for callers that just want the full text.
inline std::string join_editor_lines(const EditorText& lines) { return lines.to_string(); }
// Generic fallback for any other LineSource (e.g. a read-only preview panel
// backed by a plain vector<string> via VectorLineSource).
inline std::string join_editor_lines(const LineSource& lines) {
  std::string out;
  const int count = lines.size();
  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      out.push_back('\n');
    }
    out += lines.at(i);
  }
  return out;
}

// Canonical editor source: same bytes as join_editor_lines(buffer.lines), even when
// the input came from reading a file with a trailing newline.
std::string normalize_editor_source(const std::string& source);

std::string join_editor_lines_from_file(const std::string& path);

struct DocumentEntry {
  std::string path;
  std::string source;
  TSTree* tree = nullptr;
  uint64_t revision = 0;
  std::vector<LineHighlights> line_highlights;
  std::vector<SymbolInfo> symbols;
  std::vector<SymbolInfo> scope_symbols;
  bool parse_ready = false;
  bool highlights_ready = false;
  bool symbols_ready = false;
  bool prepare_inflight = false;
  std::optional<ViewportHighlightPreview> viewport_preview;

  ~DocumentEntry();
  DocumentEntry() = default;
  DocumentEntry(const DocumentEntry&) = delete;
  DocumentEntry& operator=(const DocumentEntry&) = delete;
};

using DocumentPtr = std::shared_ptr<DocumentEntry>;

class TreeSitterDocumentCache {
 public:
  using ReadyCallback = std::function<void(const std::string& path)>;

  TreeSitterDocumentCache();
  ~TreeSitterDocumentCache();

  TreeSitterDocumentCache(const TreeSitterDocumentCache&) = delete;
  TreeSitterDocumentCache& operator=(const TreeSitterDocumentCache&) = delete;

  void set_ready_callback(ReadyCallback callback);

  DocumentPtr lookup(const std::string& path) const;
  // `edit_hint`, when present, lets this skip the O(document size)
  // prefix/suffix diff against the previous source (see single_edit_between
  // in the .cpp) and build the TSInputEdit directly -- see the "Fase 3" text
  // storage migration plan. Only trusted for small, single-edit changes (the
  // common case while typing); anything else safely falls back to diffing.
  void request_prepare(const std::string& path, const std::string& source,
                       const std::optional<EditorTextEditHint>& edit_hint = std::nullopt);
  void invalidate(const std::string& path);
  uint64_t revision_for(const std::string& path) const;
  bool document_highlights_ready(const std::string& path, const std::string& canonical) const;
  bool document_symbols_ready(const std::string& path, const std::string& canonical) const;
  void ensure_viewport_preview(const std::string& path, const std::string& canonical,
                               const std::vector<int>& line_indices);
  const LineHighlights* viewport_preview_line(const std::string& path, const std::string& canonical,
                                              int line_0) const;

  struct HighlightTreeSnapshot {
    TSTree* tree_copy = nullptr;
    std::vector<SymbolInfo> scope_symbols;
    uint64_t revision = 0;
    bool ok = false;
  };

  HighlightTreeSnapshot snapshot_for_highlight(const std::string& path,
                                               const std::string& canonical,
                                               uint64_t expected_revision) const;

 private:
  struct PrepareJob {
    std::string path;
    std::string source;
    std::string previous_source;
    TSTree* previous_tree = nullptr;
  };

  struct DebouncedPrepare {
    std::string path;
    std::string source;
    std::string previous_source;
    TSTree* previous_tree = nullptr;
    std::chrono::steady_clock::time_point run_after{};
  };

  void reset_entry(DocumentEntry* entry);
  void worker_main();
  void run_prepare(PrepareJob job);
  void flush_ready_debounce_jobs(std::vector<PrepareJob>* ready_jobs);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, DocumentPtr> cache_;
  uint64_t next_revision_ = 1;

  std::mutex worker_mutex_;
  std::condition_variable worker_cv_;
  std::thread worker_;
  std::atomic<bool> worker_stop_{false};
  std::vector<PrepareJob> pending_jobs_;
  std::unordered_map<std::string, DebouncedPrepare> debounce_jobs_;

  std::mutex callback_mutex_;
  ReadyCallback ready_callback_;
};

TSPoint make_ts_point(int line_0, int col);
bool ts_point_in_node(TSNode node, TSPoint point);

}  // namespace tgdb
