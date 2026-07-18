#pragma once

#include <string>
#include <vector>

namespace tuide {

struct WorkspaceModel;

struct CursorHistoryEntry {
  std::string path;
  int line = 0;
  int col = 0;

  bool operator==(const CursorHistoryEntry& other) const {
    return path == other.path && line == other.line && col == other.col;
  }
};

class CursorHistory {
 public:
  void record_jump(WorkspaceModel* workspace);
  bool go_back(WorkspaceModel* workspace, int visible_lines);
  bool go_forward(WorkspaceModel* workspace, int visible_lines);
  void clear();

 private:
  std::vector<CursorHistoryEntry> back_;
  std::vector<CursorHistoryEntry> forward_;
  bool navigating_ = false;

  CursorHistoryEntry current_entry(WorkspaceModel& workspace) const;
  void apply_entry(WorkspaceModel* workspace, const CursorHistoryEntry& entry,
                   int visible_lines);
};

}  // namespace tuide
