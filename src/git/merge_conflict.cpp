#include "git/merge_conflict.hpp"

#include <algorithm>

namespace tuide {

namespace {

constexpr const char* kOursMarker = "<<<<<<<";
constexpr const char* kBaseMarker = "|||||||";
constexpr const char* kSepMarker = "=======";
constexpr const char* kTheirsMarker = ">>>>>>>";
constexpr std::size_t kMarkerLen = 7;

bool has_marker(const std::string& line, const char* marker) {
  return line.size() >= kMarkerLen && line.compare(0, kMarkerLen, marker) == 0;
}

std::string marker_label(const std::string& line) {
  if (line.size() <= kMarkerLen) {
    return {};
  }
  std::size_t begin = kMarkerLen;
  while (begin < line.size() && line[begin] == ' ') {
    ++begin;
  }
  std::size_t end = line.size();
  while (end > begin && (line[end - 1] == '\r' || line[end - 1] == '\n')) {
    --end;
  }
  return line.substr(begin, end - begin);
}

enum class ParseState {
  kNone,
  kInOurs,
  kInBase,
  kInTheirs,
};

}  // namespace

ConflictParseResult parse_conflict_markers(const std::vector<std::string>& lines) {
  ConflictParseResult result;
  ParseState state = ParseState::kNone;
  ConflictHunk current;

  for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
    const std::string& line = lines[static_cast<std::size_t>(i)];
    switch (state) {
      case ParseState::kNone:
        if (has_marker(line, kOursMarker)) {
          current = ConflictHunk{};
          current.start_line = i;
          current.ours_begin = i + 1;
          current.ours_label = marker_label(line);
          state = ParseState::kInOurs;
        }
        break;
      case ParseState::kInOurs:
        if (has_marker(line, kBaseMarker)) {
          current.ours_end = i;
          current.has_base = true;
          current.base_begin = i + 1;
          current.base_label = marker_label(line);
          state = ParseState::kInBase;
        } else if (has_marker(line, kSepMarker)) {
          current.ours_end = i;
          current.theirs_begin = i + 1;
          state = ParseState::kInTheirs;
        } else if (has_marker(line, kOursMarker)) {
          // Marcador de apertura sin cierre: se descarta y se reinicia desde aquí.
          current = ConflictHunk{};
          current.start_line = i;
          current.ours_begin = i + 1;
          current.ours_label = marker_label(line);
        }
        break;
      case ParseState::kInBase:
        if (has_marker(line, kSepMarker)) {
          current.base_end = i;
          current.theirs_begin = i + 1;
          state = ParseState::kInTheirs;
        } else if (has_marker(line, kOursMarker)) {
          current = ConflictHunk{};
          current.start_line = i;
          current.ours_begin = i + 1;
          current.ours_label = marker_label(line);
          state = ParseState::kInOurs;
        }
        break;
      case ParseState::kInTheirs:
        if (has_marker(line, kTheirsMarker)) {
          current.theirs_end = i;
          current.end_line = i;
          current.theirs_label = marker_label(line);
          result.hunks.push_back(current);
          state = ParseState::kNone;
        } else if (has_marker(line, kOursMarker)) {
          // Cierre ausente: se descarta el hunk a medias y se reinicia.
          current = ConflictHunk{};
          current.start_line = i;
          current.ours_begin = i + 1;
          current.ours_label = marker_label(line);
          state = ParseState::kInOurs;
        }
        break;
    }
  }
  // Un hunk abierto que nunca cerró (marcador ">>>>>>>" ausente) se descarta.
  return result;
}

namespace {

std::vector<std::string> slice(const std::vector<std::string>& lines, int begin, int end) {
  std::vector<std::string> out;
  const int total = static_cast<int>(lines.size());
  begin = std::max(0, std::min(begin, total));
  end = std::max(begin, std::min(end, total));
  out.reserve(static_cast<std::size_t>(end - begin));
  for (int i = begin; i < end; ++i) {
    out.push_back(lines[static_cast<std::size_t>(i)]);
  }
  return out;
}

}  // namespace

std::vector<std::string> conflict_hunk_side_lines(const std::vector<std::string>& lines,
                                                   const ConflictHunk& hunk, ConflictSide side) {
  switch (side) {
    case ConflictSide::kOurs:
      return slice(lines, hunk.ours_begin, hunk.ours_end);
    case ConflictSide::kBase:
      return hunk.has_base ? slice(lines, hunk.base_begin, hunk.base_end)
                            : std::vector<std::string>{};
    case ConflictSide::kTheirs:
      return slice(lines, hunk.theirs_begin, hunk.theirs_end);
  }
  return {};
}

std::vector<std::string> conflict_hunk_both_lines(const std::vector<std::string>& lines,
                                                   const ConflictHunk& hunk) {
  std::vector<std::string> out = slice(lines, hunk.ours_begin, hunk.ours_end);
  std::vector<std::string> theirs = slice(lines, hunk.theirs_begin, hunk.theirs_end);
  out.insert(out.end(), theirs.begin(), theirs.end());
  return out;
}

int find_next_conflict_hunk(const std::vector<ConflictHunk>& hunks, int current_line) {
  if (hunks.empty()) {
    return -1;
  }
  for (int i = 0; i < static_cast<int>(hunks.size()); ++i) {
    if (hunks[static_cast<std::size_t>(i)].start_line > current_line) {
      return i;
    }
  }
  return 0;
}

int find_prev_conflict_hunk(const std::vector<ConflictHunk>& hunks, int current_line) {
  if (hunks.empty()) {
    return -1;
  }
  for (int i = static_cast<int>(hunks.size()) - 1; i >= 0; --i) {
    if (hunks[static_cast<std::size_t>(i)].end_line < current_line) {
      return i;
    }
  }
  return static_cast<int>(hunks.size()) - 1;
}

int conflict_hunk_at_line(const std::vector<ConflictHunk>& hunks, int line) {
  for (int i = 0; i < static_cast<int>(hunks.size()); ++i) {
    const ConflictHunk& hunk = hunks[static_cast<std::size_t>(i)];
    if (line >= hunk.start_line && line <= hunk.end_line) {
      return i;
    }
  }
  return -1;
}

ConflictLineBand conflict_line_band(const std::vector<ConflictHunk>& hunks, int line) {
  const int index = conflict_hunk_at_line(hunks, line);
  if (index < 0) {
    return ConflictLineBand::kNone;
  }
  const ConflictHunk& hunk = hunks[static_cast<std::size_t>(index)];
  if (line < hunk.ours_end) {
    return ConflictLineBand::kOurs;
  }
  if (hunk.has_base && line < hunk.base_end) {
    return ConflictLineBand::kBase;
  }
  return ConflictLineBand::kTheirs;
}

}  // namespace tuide
