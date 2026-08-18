#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai/ai_types.hpp"
#include "app/workspace_model.hpp"
#include "editor/editor_state.hpp"

namespace tuide {

class EditJournalStore {
 public:
  static EditJournalStore& instance();

  uint64_t next_op_id();

  void record_edit(const std::string& path, AiTextEdit edit);
  void mark_lines(const std::string& path, int start_line_1, int end_line_1, AiAuthor author,
                  uint64_t op_id);

  AiAuthor author_for_line(const std::string& path, int line_1based) const;
  bool line_is_ai(const std::string& path, int line_1based) const;

  // Apply a replace in the given buffer (or open tab), push undo, journal AI author.
  bool apply_replace(WorkspaceModel* workspace, const std::string& path, int start_line,
                     int start_col, int end_line, int end_col, const std::string& new_text,
                     AiAuthor author, std::string* error);

  // Demo apply used by Fase A: inserts a comment near the cursor and paints gutter blue.
  bool apply_demo(WorkspaceModel* workspace, std::string* detail);

  void save_sidecar(const std::string& workspace_root, const std::string& path) const;
  void load_sidecar(const std::string& workspace_root, const std::string& path,
                    const std::string& file_hash);
  void invalidate(const std::string& path);

 private:
  struct FileJournal {
    std::vector<AiTextEdit> edits;
    std::unordered_map<int, AiAuthor> line_authors;
    std::string content_hash;
  };

  FileJournal* mutable_journal(const std::string& path);
  const FileJournal* journal(const std::string& path) const;
  static std::string sidecar_path(const std::string& workspace_root, const std::string& path);
  static std::string hash_text(const std::string& text);

  mutable std::mutex mu_;
  std::unordered_map<std::string, FileJournal> by_path_;
  uint64_t next_op_ = 1;
};

}  // namespace tuide
