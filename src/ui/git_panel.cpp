#include "ui/git_panel.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <set>
#include <sstream>
#include <vector>

#include "app/workspace_model.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/focusable_component.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/scroll_bar.hpp"
#include "ui/spinner.hpp"
#include "ui/text_input_style.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tuide {

using namespace ftxui;

namespace {


constexpr int kFileListWidthMin = 18;
constexpr int kFileListDiffReserve = 24;

constexpr const char* kGitTab0 = "git-tab-0";
constexpr const char* kGitTab1 = "git-tab-1";
constexpr const char* kGitTab2 = "git-tab-2";
constexpr const char* kGitTab3 = "git-tab-3";
constexpr const char* kGitTab4 = "git-tab-4";
constexpr const char* kGitStage = "git-stage";
constexpr const char* kGitUnstage = "git-unstage";
constexpr const char* kGitPush = "git-push";
constexpr const char* kGitPull = "git-pull";
constexpr const char* kGitCommit = "git-commit";
constexpr const char* kGitViewToggle = "git-view-toggle";
constexpr const char* kGitSearch = "git-search";
constexpr const char* kGitCommitConfirm = "git-commit-confirm";
constexpr const char* kGitCommitCancel = "git-commit-cancel";
constexpr const char* kGitListScrollbar = "git-list-scrollbar";
constexpr const char* kGitDiffScrollbar = "git-diff-scrollbar";

struct StatusVisualRow {
  bool is_dir = false;
  int depth = 0;
  int entry_index = -1;
  std::string name;
  std::string dir;
  std::string dir_prefix;
};

std::string status_badge(const GitStatusEntry& entry) {
  std::string badge;
  if (entry.staged != GitFileStatus::kUnmodified && entry.staged != GitFileStatus::kUntracked &&
      entry.staged != GitFileStatus::kIgnored) {
    switch (entry.staged) {
      case GitFileStatus::kModified:
        badge += "M";
        break;
      case GitFileStatus::kAdded:
        badge += "A";
        break;
      case GitFileStatus::kDeleted:
        badge += "D";
        break;
      case GitFileStatus::kRenamed:
        badge += "R";
        break;
      default:
        badge += "?";
        break;
    }
  }
  if (entry.unstaged != GitFileStatus::kUnmodified) {
    switch (entry.unstaged) {
      case GitFileStatus::kModified:
        badge += "M";
        break;
      case GitFileStatus::kAdded:
        badge += "A";
        break;
      case GitFileStatus::kDeleted:
        badge += "D";
        break;
      case GitFileStatus::kUntracked:
        badge += "?";
        break;
      default:
        badge += ".";
        break;
    }
  }
  if (badge.empty()) {
    badge = " ";
  }
  return badge;
}

Color status_color(const GitStatusEntry& entry) {
  if (entry.unstaged == GitFileStatus::kUntracked || entry.staged == GitFileStatus::kUntracked) {
    return theme::Warning();
  }
  if (entry.staged != GitFileStatus::kUnmodified && entry.staged != GitFileStatus::kIgnored) {
    return theme::Success();
  }
  return theme::Accent();
}

bool entry_is_staged(const GitStatusEntry& entry) {
  return entry.staged != GitFileStatus::kUnmodified && entry.staged != GitFileStatus::kUntracked &&
         entry.staged != GitFileStatus::kIgnored;
}

bool entry_is_untracked(const GitStatusEntry& entry) {
  return entry.unstaged == GitFileStatus::kUntracked || entry.staged == GitFileStatus::kUntracked;
}

int entry_sort_rank(const GitStatusEntry& entry) {
  // staged → modified → untracked. Porcelain "??" sets both sides to '?'.
  if (entry_is_untracked(entry)) {
    return 2;
  }
  if (entry_is_staged(entry)) {
    return 0;
  }
  return 1;
}

void split_path_display(const std::string& path, std::string* name, std::string* dir) {
  if (name == nullptr || dir == nullptr) {
    return;
  }
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    *name = path;
    *dir = "";
    return;
  }
  *name = path.substr(slash + 1);
  *dir = path.substr(0, slash + 1);
}

std::string to_lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool path_matches_filter(const std::string& path, const std::string& filter) {
  if (filter.empty()) {
    return true;
  }
  return to_lower_copy(path).find(to_lower_copy(filter)) != std::string::npos;
}

std::vector<int> visible_status_indices(const std::vector<GitStatusEntry>& entries,
                                        const std::string& filter) {
  std::vector<int> indices;
  indices.reserve(entries.size());
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (path_matches_filter(entries[static_cast<std::size_t>(i)].path, filter)) {
      indices.push_back(i);
    }
  }
  std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
    const auto& left = entries[static_cast<std::size_t>(a)];
    const auto& right = entries[static_cast<std::size_t>(b)];
    const int rank_a = entry_sort_rank(left);
    const int rank_b = entry_sort_rank(right);
    if (rank_a != rank_b) {
      return rank_a < rank_b;
    }
    return left.path < right.path;
  });
  return indices;
}

std::vector<StatusVisualRow> build_status_visual_rows(const std::vector<GitStatusEntry>& entries,
                                                     const std::vector<int>& indices,
                                                     bool tree_view) {
  std::vector<StatusVisualRow> rows;
  if (!tree_view) {
    rows.reserve(indices.size());
    for (int idx : indices) {
      const auto& entry = entries[static_cast<std::size_t>(idx)];
      StatusVisualRow row;
      row.entry_index = idx;
      split_path_display(entry.path, &row.name, &row.dir);
      rows.push_back(std::move(row));
    }
    return rows;
  }

  std::set<std::string> inserted_dirs;
  for (int idx : indices) {
    const std::string& path = entries[static_cast<std::size_t>(idx)].path;
    std::size_t start = 0;
    int depth = 0;
    while (true) {
      const auto slash = path.find('/', start);
      if (slash == std::string::npos) {
        break;
      }
      const std::string dir_path = path.substr(0, slash + 1);
      if (inserted_dirs.insert(dir_path).second) {
        StatusVisualRow dir_row;
        dir_row.is_dir = true;
        dir_row.depth = depth;
        dir_row.name = path.substr(start, slash - start) + "/";
        dir_row.dir_prefix = dir_path;
        rows.push_back(std::move(dir_row));
      }
      start = slash + 1;
      ++depth;
    }
    StatusVisualRow file_row;
    file_row.depth = depth;
    file_row.entry_index = idx;
    file_row.name = path.substr(start);
    rows.push_back(std::move(file_row));
  }
  return rows;
}

int visual_row_for_entry(const std::vector<StatusVisualRow>& rows, int entry_index) {
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    if (!rows[static_cast<std::size_t>(i)].is_dir &&
        rows[static_cast<std::size_t>(i)].entry_index == entry_index) {
      return i;
    }
  }
  return -1;
}

Element render_status_file_row(const GitStatusEntry& entry, const StatusVisualRow& row,
                               bool selected) {
  const Color entry_color = row.is_dir ? theme::Muted() : status_color(entry);
  const std::string indent(static_cast<std::size_t>(std::max(0, row.depth * 2)), ' ');
  Element content;
  if (row.is_dir) {
    content = hbox({
        text(" " + indent),
        text(row.name) | color(entry_color) | bold,
        filler(),
    });
  } else if (!row.dir.empty()) {
    content = hbox({
        text(" " + indent + status_badge(entry) + " ") | color(entry_color),
        text(row.name) | color(entry_color) | bold,
        filler(),
        text(row.dir) | color(theme::Muted()),
    });
  } else {
    content = hbox({
        text(" " + indent + status_badge(entry) + " ") | color(entry_color),
        text(row.name) | color(entry_color) | bold,
        filler(),
    });
  }
  return StyleListRow(std::move(content), selected, false, false);
}

bool mouse_control_active(const Mouse& m, const Event& event) {
  if (m.control) {
    return true;
  }
  if (!event.is_mouse()) {
    return false;
  }
  const std::string& input = event.input();
  if (input.size() < 6 || input[0] != '\x1b' || input[1] != '[' || input[2] != '<') {
    return false;
  }
  int button = 0;
  for (std::size_t i = 3; i < input.size() && input[i] != ';' && input[i] != 'M' && input[i] != 'm';
       ++i) {
    button = button * 10 + (input[i] - '0');
  }
  return (button & 16) != 0;
}

std::vector<int> entry_indices_under_prefix(const std::vector<GitStatusEntry>& entries,
                                            const std::vector<int>& indices,
                                            const std::string& prefix) {
  std::vector<int> out;
  if (prefix.empty()) {
    return out;
  }
  for (int idx : indices) {
    if (idx < 0 || idx >= static_cast<int>(entries.size())) {
      continue;
    }
    const std::string& path = entries[static_cast<std::size_t>(idx)].path;
    if (path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0) {
      out.push_back(idx);
    }
  }
  return out;
}

bool dir_row_selected(const StatusVisualRow& row, const std::vector<GitStatusEntry>& entries,
                      const std::vector<int>& indices, const std::set<int>& multi_selected) {
  if (!row.is_dir || row.dir_prefix.empty()) {
    return false;
  }
  const auto kids = entry_indices_under_prefix(entries, indices, row.dir_prefix);
  if (kids.empty()) {
    return false;
  }
  for (int idx : kids) {
    if (multi_selected.count(idx) == 0) {
      return false;
    }
  }
  return true;
}

void clear_multi_selection(GitPanelState* state) {
  if (state != nullptr) {
    state->multi_selected.clear();
  }
}

void toggle_multi_entry(GitPanelState* state, int entry_index) {
  if (state == nullptr || entry_index < 0) {
    return;
  }
  const auto it = state->multi_selected.find(entry_index);
  if (it == state->multi_selected.end()) {
    state->multi_selected.insert(entry_index);
  } else {
    state->multi_selected.erase(it);
  }
}

void set_multi_entries(GitPanelState* state, const std::vector<int>& entry_indices, bool toggle) {
  if (state == nullptr) {
    return;
  }
  if (!toggle) {
    state->multi_selected.clear();
    for (int idx : entry_indices) {
      if (idx >= 0) {
        state->multi_selected.insert(idx);
      }
    }
    return;
  }
  bool all_selected = !entry_indices.empty();
  for (int idx : entry_indices) {
    if (state->multi_selected.count(idx) == 0) {
      all_selected = false;
      break;
    }
  }
  if (all_selected) {
    for (int idx : entry_indices) {
      state->multi_selected.erase(idx);
    }
  } else {
    for (int idx : entry_indices) {
      if (idx >= 0) {
        state->multi_selected.insert(idx);
      }
    }
  }
}

void clamp_selection(GitPanelState* state, int count);

std::vector<std::string> paths_for_stage_action(GitService* git, GitPanelState* state) {
  std::vector<std::string> paths;
  if (git == nullptr || state == nullptr) {
    return paths;
  }
  const GitStatusSnapshot status = git->status();
  const auto indices = visible_status_indices(status.entries, state->search_query);
  if (!state->multi_selected.empty()) {
    for (int idx : state->multi_selected) {
      if (idx >= 0 && idx < static_cast<int>(status.entries.size())) {
        paths.push_back(status.entries[static_cast<std::size_t>(idx)].path);
      }
    }
    return paths;
  }
  if (indices.empty()) {
    return paths;
  }
  clamp_selection(state, static_cast<int>(indices.size()));
  const int entry_index = indices[static_cast<std::size_t>(state->selected_file)];
  if (entry_index >= 0 && entry_index < static_cast<int>(status.entries.size())) {
    paths.push_back(status.entries[static_cast<std::size_t>(entry_index)].path);
  }
  return paths;
}

Element MakeCompactTabButton(const std::string& label, bool selected, bool hovered, bool pressed,
                             Box* box) {
  Element tab = text(" " + label + " ") | center | size(HEIGHT, EQUAL, 1);
  tab = StyleClickable(std::move(tab), {selected, hovered, pressed, false});
  return tab | reflect(*box);
}

Element MakeIconButton(const std::string& glyph, bool hovered, bool pressed, Box* box) {
  return MakeToolbarButton(text(" " + glyph + " ") | bold | color(theme::Header()), hovered,
                           pressed, false, box, true);
}

Element render_diff_lines(const std::string& diff_text, bool loading, int scroll, int height) {
  if (loading) {
    return text(i18n::tr("git.diff.loading")) | color(theme::Muted());
  }
  std::vector<std::string> lines;
  std::istringstream stream(diff_text);
  std::string line;
  constexpr std::size_t kMaxDiffLines = 500;
  while (lines.size() < kMaxDiffLines && std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }
  Elements rows;
  const int end = std::min(static_cast<int>(lines.size()), scroll + height);
  for (int i = scroll; i < end; ++i) {
    const std::string& row = lines[static_cast<std::size_t>(i)];
    Color row_color = theme::Header();
    if (!row.empty()) {
      if (row[0] == '+') {
        row_color = theme::Success();
      } else if (row[0] == '-') {
        row_color = theme::Error();
      } else if (row[0] == '@') {
        row_color = theme::Accent();
      }
    }
    rows.push_back(text(" " + row) | color(row_color));
  }
  if (rows.empty()) {
    rows.push_back(text(i18n::tr("git.diff.no_changes")) | color(theme::Muted()));
  }
  return vbox(std::move(rows)) | flex;
}

struct LogCommitViewLine {
  enum class Kind { FileHeader, DiffLine, Muted };
  Kind kind = Kind::DiffLine;
  std::string text;
};

Color log_commit_line_color(const LogCommitViewLine& row) {
  if (row.kind == LogCommitViewLine::Kind::FileHeader) {
    return theme::Accent();
  }
  if (row.kind == LogCommitViewLine::Kind::Muted) {
    return theme::Muted();
  }
  if (row.text.empty()) {
    return theme::Muted();
  }
  if (row.text[0] == '+') {
    return theme::Success();
  }
  if (row.text[0] == '-') {
    return theme::Error();
  }
  if (row.text[0] == '@') {
    return theme::Accent();
  }
  return theme::Header();
}

std::vector<LogCommitViewLine> build_log_commit_view_lines(
    GitService* git, const std::string& commit_hash,
    const std::vector<GitCommitFileEntry>& files) {
  std::vector<LogCommitViewLine> rows;
  if (git == nullptr || files.empty()) {
    return rows;
  }
  constexpr std::size_t kMaxLinesPerFile = 250;
  constexpr std::size_t kMaxTotalLines = 2000;
  for (const auto& file : files) {
    if (rows.size() >= kMaxTotalLines) {
      break;
    }
    rows.push_back({LogCommitViewLine::Kind::FileHeader, file.status + " " + file.path});
    const std::string diff = git->timeline_diff_text(file.path, commit_hash);
    if (diff.empty()) {
      rows.push_back({LogCommitViewLine::Kind::Muted,
                      git->busy() ? i18n::tr("git.diff.loading") : i18n::tr("git.diff.no_changes")});
      rows.push_back({LogCommitViewLine::Kind::Muted, ""});
      continue;
    }
    std::istringstream stream(diff);
    std::string line;
    std::size_t file_lines = 0;
    while (file_lines < kMaxLinesPerFile && rows.size() < kMaxTotalLines &&
           std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      rows.push_back({LogCommitViewLine::Kind::DiffLine, line});
      ++file_lines;
    }
    rows.push_back({LogCommitViewLine::Kind::Muted, ""});
  }
  return rows;
}

Element render_log_commit_view(const std::vector<LogCommitViewLine>& lines, int scroll,
                               int height) {
  Elements rows;
  const int end = std::min(static_cast<int>(lines.size()), scroll + height);
  for (int i = scroll; i < end; ++i) {
    const auto& row = lines[static_cast<std::size_t>(i)];
    if (row.kind == LogCommitViewLine::Kind::FileHeader) {
      rows.push_back(text(" " + row.text) | color(theme::Accent()) | bold);
    } else if (row.text.empty()) {
      rows.push_back(text(" "));
    } else {
      rows.push_back(text(" " + row.text) | color(log_commit_line_color(row)));
    }
  }
  if (rows.empty()) {
    rows.push_back(text(i18n::tr("git.log.no_files")) | color(theme::Muted()));
  }
  return vbox(std::move(rows)) | flex;
}

void set_status(GitPanelState* state, const std::string& message) {
  if (state != nullptr) {
    state->status_message = message;
  }
}

bool is_double_click(int index, int* last_index, int64_t* last_ms) {
  using namespace std::chrono;
  const int64_t now =
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  const bool doubled = index == *last_index && *last_index >= 0 && (now - *last_ms) < 450;
  *last_index = index;
  *last_ms = now;
  return doubled;
}

void open_status_file_diff(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                           FocusManagerState* focus, WorkspaceModel* workspace, int entry_index) {
  if (git == nullptr || state == nullptr || layout_state == nullptr || workspace == nullptr) {
    return;
  }
  // status() returns by value: never bind references to its members (dangling → bad_alloc).
  const GitStatusSnapshot status = git->status();
  if (entry_index < 0 || entry_index >= static_cast<int>(status.entries.size())) {
    return;
  }
  if (layout_state->git_open_diff_view) {
    layout_state->git_open_diff_view(status.entries[static_cast<std::size_t>(entry_index)].path);
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    UI_WAKE(layout_state, "git.diff.open");
  }
}

std::vector<GitCommitEntry> visible_log_commits(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr) {
    return {};
  }
  if (state->log_search_applied.empty()) {
    return git->log_entries();
  }
  if (git->log_search_ready(state->log_search_applied)) {
    return git->log_search_results();
  }
  return {};
}

void apply_log_search(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr) {
    return;
  }
  state->log_search_applied = state->search_query;
  state->selected_file = 0;
  state->log_scroll = 0;
  state->last_log_commit.clear();
  state->diff_scroll = 0;
  if (!state->log_search_applied.empty()) {
    git->refresh_log_search(state->log_search_applied);
  }
}

void clamp_selection(GitPanelState* state, int count) {
  if (state == nullptr) {
    return;
  }
  if (count <= 0) {
    state->selected_file = 0;
    return;
  }
  if (state->selected_file < 0) {
    state->selected_file = 0;
  }
  if (state->selected_file >= count) {
    state->selected_file = count - 1;
  }
}

int* active_list_scroll(GitPanelState* state) {
  if (state == nullptr) {
    return nullptr;
  }
  if (state->selected_tab == GitPanelState::kTabLog) {
    return &state->log_scroll;
  }
  if (state->selected_tab == GitPanelState::kTabBranches) {
    return &state->branch_scroll;
  }
  if (state->selected_tab == GitPanelState::kTabTimeline) {
    return &state->timeline_scroll;
  }
  return &state->file_scroll;
}

void clamp_list_scroll(GitPanelState* state, int total, int visible) {
  if (state == nullptr || visible <= 0) {
    return;
  }
  int* scroll = active_list_scroll(state);
  if (scroll == nullptr) {
    return;
  }
  *scroll = std::max(0, std::min(*scroll, std::max(0, total - visible)));
}

void scroll_selection_into_view(GitPanelState* state, int count, int visible) {
  if (state == nullptr || visible <= 0) {
    return;
  }
  int* scroll = active_list_scroll(state);
  if (scroll == nullptr) {
    return;
  }
  if (state->selected_file < *scroll) {
    *scroll = state->selected_file;
  } else if (state->selected_file >= *scroll + visible) {
    *scroll = state->selected_file - visible + 1;
  }
  *scroll = std::max(0, std::min(*scroll, std::max(0, count - visible)));
}

void scroll_status_selection_into_view(GitPanelState* state,
                                       const std::vector<StatusVisualRow>& rows, int entry_index,
                                       int visible) {
  if (state == nullptr || visible <= 0 || rows.empty() || entry_index < 0) {
    return;
  }
  const int visual = visual_row_for_entry(rows, entry_index);
  if (visual < 0) {
    return;
  }
  if (visual < state->file_scroll) {
    state->file_scroll = visual;
  } else if (visual >= state->file_scroll + visible) {
    state->file_scroll = visual - visible + 1;
  }
  state->file_scroll =
      std::max(0, std::min(state->file_scroll, std::max(0, static_cast<int>(rows.size()) - visible)));
}

int count_diff_lines(const std::string& diff_text) {
  if (diff_text.empty()) {
    return 0;
  }
  int lines = 1;
  for (char ch : diff_text) {
    if (ch == '\n') {
      ++lines;
    }
  }
  return lines;
}

int list_scroll_offset(GitPanelState* state) {
  if (state == nullptr) {
    return 0;
  }
  if (state->selected_tab == GitPanelState::kTabLog) {
    return state->log_scroll;
  }
  if (state->selected_tab == GitPanelState::kTabBranches) {
    return state->branch_scroll;
  }
  if (state->selected_tab == GitPanelState::kTabTimeline) {
    return state->timeline_scroll;
  }
  return state->file_scroll;
}

int list_index_from_mouse(GitPanelState* state, int local_row) {
  if (state == nullptr) {
    return 0;
  }
  return list_scroll_offset(state) + local_row;
}

Element render_graph_line(const std::string& line) {
  std::size_t prefix_end = 0;
  while (prefix_end < line.size()) {
    const char ch = line[prefix_end];
    if (ch == ' ' || ch == '|' || ch == '*' || ch == '\\' || ch == '/' || ch == '-' ||
        ch == '+' || ch == '_') {
      ++prefix_end;
    } else {
      break;
    }
  }
  if (prefix_end == 0) {
    return text(" " + line) | color(theme::Header());
  }
  const std::string prefix = line.substr(0, prefix_end);
  const std::string rest = line.substr(prefix_end);
  const auto open = rest.find('(');
  const auto close = rest.find(')', open == std::string::npos ? 0 : open);
  if (open != std::string::npos && close != std::string::npos && close > open) {
    return hbox({
        text(" " + prefix) | color(theme::Accent()),
        text(rest.substr(0, open)) | color(theme::Header()),
        text(rest.substr(open, close - open + 1)) | color(theme::Success()),
        text(rest.substr(close + 1)) | color(theme::Header()),
    });
  }
  return hbox({
      text(" " + prefix) | color(theme::Accent()),
      text(rest) | color(theme::Header()),
  });
}

void clamp_diff_scroll(GitPanelState* state, int diff_lines, int visible) {
  if (state == nullptr || visible <= 0) {
    return;
  }
  state->diff_scroll = std::max(0, std::min(state->diff_scroll, std::max(0, diff_lines - visible)));
}

bool scroll_git_list(GitPanelState* state, int delta, int count, int visible) {
  if (state == nullptr || delta == 0) {
    return false;
  }
  int* scroll = active_list_scroll(state);
  if (scroll == nullptr) {
    return false;
  }
  const int max_scroll = std::max(0, count - visible);
  const int next = std::max(0, std::min(*scroll + delta, max_scroll));
  if (next == *scroll) {
    return false;
  }
  *scroll = next;
  return true;
}

bool scroll_git_diff(GitPanelState* state, int delta, int diff_lines, int visible) {
  if (state == nullptr || delta == 0) {
    return false;
  }
  const int max_scroll = std::max(0, diff_lines - visible);
  const int next = std::max(0, std::min(state->diff_scroll + delta, max_scroll));
  if (next == state->diff_scroll) {
    return false;
  }
  state->diff_scroll = next;
  return true;
}

bool scroll_git_graph(GitPanelState* state, int delta, int line_count, int visible) {
  if (state == nullptr || delta == 0) {
    return false;
  }
  const int max_scroll = std::max(0, line_count - visible);
  const int next = std::max(0, std::min(state->graph_scroll + delta, max_scroll));
  if (next == state->graph_scroll) {
    return false;
  }
  state->graph_scroll = next;
  return true;
}

void reset_diff_scroll_for_path(GitPanelState* state, const std::string& path) {
  if (state == nullptr) {
    return;
  }
  if (path != state->last_diff_path) {
    state->last_diff_path = path;
    state->diff_scroll = 0;
  }
}

void reset_diff_scroll_for_commit(GitPanelState* state, const std::string& commit) {
  if (state == nullptr) {
    return;
  }
  if (commit != state->last_timeline_commit) {
    state->last_timeline_commit = commit;
    state->diff_scroll = 0;
  }
}

int selected_status_entry_index(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr) {
    return -1;
  }
  const GitStatusSnapshot status = git->status();
  const auto indices = visible_status_indices(status.entries, state->search_query);
  if (indices.empty()) {
    return -1;
  }
  clamp_selection(state, static_cast<int>(indices.size()));
  const int entry_index = indices[static_cast<std::size_t>(state->selected_file)];
  if (entry_index < 0 || entry_index >= static_cast<int>(status.entries.size())) {
    return -1;
  }
  return entry_index;
}

void ensure_selected_diff(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr || state->selected_tab != GitPanelState::kTabStatus) {
    return;
  }
  // Keep one snapshot: binding refs to git->status().entries/.path is UB (dangling).
  const GitStatusSnapshot status = git->status();
  const auto indices = visible_status_indices(status.entries, state->search_query);
  if (indices.empty()) {
    return;
  }
  clamp_selection(state, static_cast<int>(indices.size()));
  const int entry_index = indices[static_cast<std::size_t>(state->selected_file)];
  if (entry_index < 0 || entry_index >= static_cast<int>(status.entries.size())) {
    return;
  }
  const std::string path = status.entries[static_cast<std::size_t>(entry_index)].path;
  git->set_context_from_path(path);
  reset_diff_scroll_for_path(state, path);
  if (!git->has_file_diff_text(path)) {
    git->refresh_file_diff(path);
  }
}

void ensure_commit_files(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr || state->selected_tab != GitPanelState::kTabLog) {
    return;
  }
  const auto commits = visible_log_commits(git, state);
  clamp_selection(state, static_cast<int>(commits.size()));
  if (commits.empty()) {
    return;
  }
  const std::string& hash = commits[static_cast<std::size_t>(state->selected_file)].hash;
  if (hash != state->last_log_commit) {
    state->last_log_commit = hash;
    state->diff_scroll = 0;
  }
  if (!git->has_commit_files(hash)) {
    git->refresh_commit_files(hash);
    return;
  }
  const auto files = git->commit_files(hash);
  for (const auto& file : files) {
    if (!git->has_timeline_diff_text(file.path, hash)) {
      git->refresh_timeline_diff(file.path, hash);
    }
  }
}

void ensure_file_timeline(GitService* git, GitPanelState* state, const std::string& active_file) {
  if (git == nullptr || state == nullptr || state->selected_tab != GitPanelState::kTabTimeline) {
    return;
  }
  if (active_file.empty()) {
    return;
  }
  if (active_file != state->timeline_file) {
    state->timeline_file = active_file;
    state->selected_file = 0;
    state->timeline_scroll = 0;
    state->diff_scroll = 0;
    state->last_timeline_commit.clear();
    git->refresh_file_timeline(active_file);
  }
  const auto commits = git->file_timeline(active_file);
  clamp_selection(state, static_cast<int>(commits.size()));
  if (commits.empty()) {
    return;
  }
  const std::string& hash = commits[static_cast<std::size_t>(state->selected_file)].hash;
  reset_diff_scroll_for_commit(state, hash);
  if (!git->has_timeline_diff_text(active_file, hash)) {
    git->refresh_timeline_diff(active_file, hash);
  }
}

void select_tab(GitPanelState* state, GitService* git, int tab) {
  if (state == nullptr) {
    return;
  }
  state->selected_tab = tab;
  state->selected_file = 0;
  state->file_scroll = 0;
  state->log_scroll = 0;
  state->branch_scroll = 0;
  state->timeline_scroll = 0;
  state->graph_scroll = 0;
  state->diff_scroll = 0;
  state->last_diff_path.clear();
  state->last_timeline_commit.clear();
  state->last_log_commit.clear();
  state->search_focus = false;
  state->search_query.clear();
  state->search_cursor = 0;
  state->log_search_applied.clear();
  clear_multi_selection(state);
  if (git == nullptr) {
    return;
  }
  if (tab == GitPanelState::kTabLog) {
    git->refresh_log();
  } else if (tab == GitPanelState::kTabBranches) {
    git->refresh_branches();
  } else if (tab == GitPanelState::kTabTimeline) {
    state->timeline_file.clear();
  } else if (tab == GitPanelState::kTabGraph) {
    git->refresh_graph();
  } else {
    git->refresh_status();
    ensure_selected_diff(git, state);
  }
}

void stage_selected(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  if (git == nullptr || state == nullptr) {
    return;
  }
  const std::vector<std::string> paths = paths_for_stage_action(git, state);
  if (paths.empty()) {
    return;
  }
  state->operation_pending = true;
  git->stage_files(paths, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    if (ok) {
      clear_multi_selection(state);
    }
    set_status(state, ok ? i18n::tr("git.status.staged") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

void unstage_selected(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  if (git == nullptr || state == nullptr) {
    return;
  }
  const std::vector<std::string> paths = paths_for_stage_action(git, state);
  if (paths.empty()) {
    return;
  }
  state->operation_pending = true;
  git->unstage_files(paths, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    if (ok) {
      clear_multi_selection(state);
    }
    set_status(state, ok ? i18n::tr("git.status.unstaged") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

void open_commit_modal(GitPanelState* state) {
  if (state == nullptr) {
    return;
  }
  state->commit_modal_open = true;
  state->search_focus = false;
  cursor_blink::show();
}

void close_commit_modal(GitPanelState* state, bool clear_message) {
  if (state == nullptr) {
    return;
  }
  state->commit_modal_open = false;
  if (clear_message) {
    state->commit_message.clear();
    state->commit_cursor = 0;
  }
}

void commit_message(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr || state->commit_message.empty()) {
    return;
  }
  state->operation_pending = true;
  const std::string message = state->commit_message;
  git->commit(message, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    if (ok) {
      close_commit_modal(state, true);
    }
    set_status(state, ok ? i18n::tr("git.status.commit_ok") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

bool handle_search_input(GitPanelState* state, GitService* git, Event event) {
  if (state == nullptr || !state->search_focus) {
    return false;
  }
  cursor_blink::show();
  if (event == Event::Return) {
    if (state->selected_tab == GitPanelState::kTabLog) {
      apply_log_search(git, state);
    }
    return true;
  }
  if (event == Event::Escape) {
    state->search_focus = false;
    return true;
  }
  if (event == Event::Backspace) {
    if (state->search_cursor > 0 &&
        state->search_cursor <= static_cast<int>(state->search_query.size())) {
      state->search_query.erase(static_cast<std::size_t>(state->search_cursor - 1), 1);
      --state->search_cursor;
      if (state->selected_tab == GitPanelState::kTabStatus) {
        state->selected_file = 0;
        state->file_scroll = 0;
      }
    }
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (!ch.empty() && ch[0] >= 32) {
      state->search_query.insert(static_cast<std::size_t>(state->search_cursor), ch);
      state->search_cursor += static_cast<int>(ch.size());
      if (state->selected_tab == GitPanelState::kTabStatus) {
        state->selected_file = 0;
        state->file_scroll = 0;
      }
    }
    return true;
  }
  return true;
}

bool handle_commit_modal_keys(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                              Event event) {
  if (state == nullptr || !state->commit_modal_open) {
    return false;
  }
  cursor_blink::show();
  if (event == Event::Return) {
    commit_message(git, state, layout_state);
    return true;
  }
  if (event == Event::Escape) {
    close_commit_modal(state, false);
    return true;
  }
  if (event == Event::Backspace) {
    if (state->commit_cursor > 0 &&
        state->commit_cursor <= static_cast<int>(state->commit_message.size())) {
      state->commit_message.erase(static_cast<std::size_t>(state->commit_cursor - 1), 1);
      --state->commit_cursor;
    }
    return true;
  }
  if (event == Event::ArrowLeft) {
    state->commit_cursor = std::max(0, state->commit_cursor - 1);
    return true;
  }
  if (event == Event::ArrowRight) {
    state->commit_cursor =
        std::min(static_cast<int>(state->commit_message.size()), state->commit_cursor + 1);
    return true;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (!ch.empty() && ch[0] >= 32) {
      state->commit_message.insert(static_cast<std::size_t>(state->commit_cursor), ch);
      state->commit_cursor += static_cast<int>(ch.size());
    }
    return true;
  }
  return true;
}

Element render_commit_modal(GitPanelState* state, MainLayoutState* layout_state) {
  if (state == nullptr || !state->commit_modal_open) {
    return text("");
  }
  std::string line = state->commit_message;
  if (state->commit_cursor >= 0 &&
      state->commit_cursor <= static_cast<int>(line.size())) {
    line.insert(static_cast<std::size_t>(state->commit_cursor), "_");
  } else {
    line.push_back('_');
  }
  const bool confirm_hovered = interaction_active(layout_state, kGitCommitConfirm);
  const bool confirm_pressed =
      layout_state != nullptr &&
      layout_state->clickable.is_pressed(std::string_view(kGitCommitConfirm));
  const bool cancel_hovered = interaction_active(layout_state, kGitCommitCancel);
  const bool cancel_pressed =
      layout_state != nullptr &&
      layout_state->clickable.is_pressed(std::string_view(kGitCommitCancel));
  Element confirm_btn =
      MakeToolbarButton(text(" " + i18n::tr("git.commit.confirm") + " "), confirm_hovered,
                        confirm_pressed, false, &state->commit_modal_confirm_box, true);
  Element cancel_btn =
      MakeToolbarButton(text(" " + i18n::tr("git.commit.cancel") + " "), cancel_hovered,
                        cancel_pressed, false, &state->commit_modal_cancel_box, true);
  return ModalWindow(
      text(i18n::tr("git.commit.modal_title")) | color(theme::Accent()),
      vbox({
          ModalInputLine(line) | size(WIDTH, EQUAL, 56),
          text(""),
          hbox({std::move(confirm_btn), text(" "), std::move(cancel_btn)}),
          text(i18n::tr("common.footer.confirm_esc")) | color(theme::Muted()),
      }));
}

bool handle_git_keys(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                     FocusManagerState* focus, WorkspaceModel* workspace, Event event) {
  if (state == nullptr || git == nullptr || !git->is_repo()) {
    return false;
  }
  if (focus != nullptr && focus->region != FocusRegion::Terminal) {
    state->search_focus = false;
    return false;
  }

  if (event_is_tuide_global_shortcut(event)) {
    return false;
  }

  if (handle_commit_modal_keys(git, state, layout_state, event)) {
    return true;
  }

  if (state->search_focus && handle_search_input(state, git, event)) {
    return true;
  }

  if (event == Event::Character('1')) {
    select_tab(state, git, GitPanelState::kTabStatus);
    return true;
  }
  if (event == Event::Character('2')) {
    select_tab(state, git, GitPanelState::kTabLog);
    return true;
  }
  if (event == Event::Character('3')) {
    select_tab(state, git, GitPanelState::kTabBranches);
    return true;
  }
  if (event == Event::Character('4')) {
    select_tab(state, git, GitPanelState::kTabTimeline);
    return true;
  }
  if (event == Event::Character('5')) {
    select_tab(state, git, GitPanelState::kTabGraph);
    return true;
  }

  if (state->selected_tab == GitPanelState::kTabStatus) {
    const GitStatusSnapshot status = git->status();
    const auto& entries = status.entries;
    const auto indices = visible_status_indices(entries, state->search_query);
    const int count = static_cast<int>(indices.size());
    const auto rows = build_status_visual_rows(entries, indices, state->tree_view);
    const int visible = state->last_list_visible;
    if ((event == Event::ArrowUp || event == Event::Character('k')) && state->selected_file > 0) {
      --state->selected_file;
      clear_multi_selection(state);
      scroll_status_selection_into_view(
          state, rows, indices[static_cast<std::size_t>(state->selected_file)], visible);
      ensure_selected_diff(git, state);
      return true;
    }
    if ((event == Event::ArrowDown || event == Event::Character('j')) && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      clear_multi_selection(state);
      scroll_status_selection_into_view(
          state, rows, indices[static_cast<std::size_t>(state->selected_file)], visible);
      ensure_selected_diff(git, state);
      return true;
    }
    if (event == Event::Return && count > 0) {
      open_status_file_diff(git, state, layout_state, focus, workspace,
                            indices[static_cast<std::size_t>(state->selected_file)]);
      return true;
    }
    if (event == Event::PageUp) {
      scroll_git_list(state, -visible, static_cast<int>(rows.size()), visible);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_git_list(state, visible, static_cast<int>(rows.size()), visible);
      return true;
    }
    if (event == Event::Character('s')) {
      stage_selected(git, state, layout_state);
      return true;
    }
    if (event == Event::Character('u')) {
      unstage_selected(git, state, layout_state);
      return true;
    }
    if (event == Event::Character('p')) {
      state->operation_pending = true;
      git->push([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? i18n::tr("git.status.push_ok") : msg);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
      });
      return true;
    }
    if (event == Event::Character('P')) {
      state->operation_pending = true;
      git->pull([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? i18n::tr("git.status.pull_ok") : msg);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
      });
      return true;
    }
    if (event == Event::Character('c')) {
      open_commit_modal(state);
      return true;
    }
    if (event == Event::Character('t')) {
      state->tree_view = !state->tree_view;
      state->file_scroll = 0;
      return true;
    }
    if (event == Event::Character('/')) {
      state->search_focus = true;
      cursor_blink::show();
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabLog) {
    if (event == Event::Character('/')) {
      state->search_focus = true;
      cursor_blink::show();
      return true;
    }
    const int count = static_cast<int>(visible_log_commits(git, state).size());
    const int visible = state->last_list_visible;
    if ((event == Event::ArrowUp || event == Event::Character('k')) && state->selected_file > 0) {
      --state->selected_file;
      scroll_selection_into_view(state, count, visible);
      ensure_commit_files(git, state);
      return true;
    }
    if ((event == Event::ArrowDown || event == Event::Character('j')) && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      scroll_selection_into_view(state, count, visible);
      ensure_commit_files(git, state);
      return true;
    }
    if (event == Event::PageUp) {
      scroll_git_list(state, -visible, count, visible);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_git_list(state, visible, count, visible);
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabBranches) {
    const int count = static_cast<int>(git->branches().size());
    const int visible = state->last_list_visible;
    if ((event == Event::ArrowUp || event == Event::Character('k')) && state->selected_file > 0) {
      --state->selected_file;
      scroll_selection_into_view(state, count, visible);
      return true;
    }
    if ((event == Event::ArrowDown || event == Event::Character('j')) && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      scroll_selection_into_view(state, count, visible);
      return true;
    }
    if (event == Event::PageUp) {
      scroll_git_list(state, -visible, count, visible);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_git_list(state, visible, count, visible);
      return true;
    }
    if (event == Event::Return) {
      if (state->selected_file >= 0 && state->selected_file < count) {
        const std::string name =
            git->branches()[static_cast<std::size_t>(state->selected_file)].name;
        state->operation_pending = true;
        git->checkout_branch(name, [state, layout_state](bool ok, const std::string& msg) {
          state->operation_pending = false;
          set_status(state, ok ? i18n::tr("git.status.branch_switched") : msg);
          if (layout_state != nullptr) {
            UI_WAKE(layout_state, "wake");
          }
        });
      }
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabTimeline) {
    const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
    ensure_file_timeline(git, state, active_file);
    const int count = static_cast<int>(git->file_timeline(active_file).size());
    const int visible = state->last_list_visible;
    if ((event == Event::ArrowUp || event == Event::Character('k')) && state->selected_file > 0) {
      --state->selected_file;
      scroll_selection_into_view(state, count, visible);
      ensure_file_timeline(git, state, active_file);
      return true;
    }
    if ((event == Event::ArrowDown || event == Event::Character('j')) && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      scroll_selection_into_view(state, count, visible);
      ensure_file_timeline(git, state, active_file);
      return true;
    }
    if (event == Event::PageUp) {
      scroll_git_list(state, -visible, count, visible);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_git_list(state, visible, count, visible);
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabGraph) {
    git->refresh_graph();
    const int count = static_cast<int>(git->graph_lines().size());
    const int visible = state->last_diff_visible;
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      scroll_git_graph(state, -1, count, visible);
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      scroll_git_graph(state, 1, count, visible);
      return true;
    }
    if (event == Event::PageUp) {
      scroll_git_graph(state, -visible, count, visible);
      return true;
    }
    if (event == Event::PageDown) {
      scroll_git_graph(state, visible, count, visible);
      return true;
    }
  }

  if (event == Event::Escape) {
    if (state->search_focus) {
      state->search_focus = false;
      return true;
    }
    if (!state->multi_selected.empty()) {
      clear_multi_selection(state);
      return true;
    }
    return false;
  }

  return false;
}

void clamp_file_list_width(GitPanelState* state, int panel_width) {
  if (state == nullptr) {
    return;
  }
  if (!state->file_list_width_custom) {
    // Por defecto: lista ~1/3, diff el resto.
    state->file_list_width = std::max(kFileListWidthMin, panel_width / 3);
  }
  const int max_width = std::max(kFileListWidthMin, std::max(0, panel_width - kFileListDiffReserve));
  state->file_list_width =
      std::max(kFileListWidthMin, std::min(state->file_list_width, max_width));
}

bool handle_git_list_scrollbar_mouse(GitPanelState* state, MainLayoutState* layout_state,
                                     Mouse& m, int total, int visible) {
  if (state == nullptr || !state->list_scrollbar_layout.scrollable) {
    return false;
  }
  const int max_scroll = std::max(0, total - visible);
  int* scroll = active_list_scroll(state);
  if (scroll == nullptr) {
    return false;
  }
  const bool in_bar =
      !state->list_scrollbar_box.IsEmpty() && state->list_scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (in_bar || state->list_scrollbar_dragging) {
      if (layout_state != nullptr) {
        layout_state->clickable.set_hover(kGitListScrollbar);
      }
    }
    if (state->list_scrollbar_dragging) {
      const int local_y = m.y - state->list_scrollbar_box.y_min;
      const int thumb_top = local_y - state->list_scrollbar_drag_offset;
      *scroll = std::max(
          0, std::min(scroll_for_thumb_top(state->list_scrollbar_layout, thumb_top), max_scroll));
      return true;
    }
    return false;
  }

  if (state->list_scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->list_scrollbar_dragging = false;
      return true;
    }
    if (m.motion == Mouse::Moved && m.button == Mouse::Left) {
      const int local_y = m.y - state->list_scrollbar_box.y_min;
      const int thumb_top = local_y - state->list_scrollbar_drag_offset;
      *scroll = std::max(
          0, std::min(scroll_for_thumb_top(state->list_scrollbar_layout, thumb_top), max_scroll));
      return true;
    }
  }

  if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
    if (!in_bar) {
      return false;
    }
    const int delta = m.button == Mouse::WheelUp ? -3 : 3;
    *scroll = std::max(0, std::min(*scroll + delta, max_scroll));
    return true;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed || !in_bar) {
    return false;
  }

  trigger_press(layout_state, std::string_view(kGitListScrollbar));
  const int local_y = m.y - state->list_scrollbar_box.y_min;
  if (scrollbar_thumb_hit(state->list_scrollbar_layout, state->list_scrollbar_box, m.x, m.y)) {
    state->list_scrollbar_dragging = true;
    state->list_scrollbar_drag_offset = local_y - state->list_scrollbar_layout.thumb_y;
  } else {
    const int thumb_top = local_y - state->list_scrollbar_layout.thumb_height / 2;
    *scroll = std::max(
        0, std::min(scroll_for_thumb_top(state->list_scrollbar_layout, thumb_top), max_scroll));
    state->list_scrollbar_dragging = true;
    state->list_scrollbar_drag_offset = state->list_scrollbar_layout.thumb_height / 2;
  }
  return true;
}

bool handle_git_diff_scrollbar_mouse(GitPanelState* state, MainLayoutState* layout_state, Mouse& m,
                                     int total, int visible) {
  if (state == nullptr || !state->diff_scrollbar_layout.scrollable) {
    return false;
  }
  const int max_scroll = std::max(0, total - visible);
  int* scroll = state->selected_tab == GitPanelState::kTabGraph ? &state->graph_scroll
                                                               : &state->diff_scroll;
  const bool in_bar =
      !state->diff_scrollbar_box.IsEmpty() && state->diff_scrollbar_box.Contain(m.x, m.y);

  if (m.motion == Mouse::Moved) {
    if (in_bar || state->diff_scrollbar_dragging) {
      if (layout_state != nullptr) {
        layout_state->clickable.set_hover(kGitDiffScrollbar);
      }
    }
    if (state->diff_scrollbar_dragging) {
      const int local_y = m.y - state->diff_scrollbar_box.y_min;
      const int thumb_top = local_y - state->diff_scrollbar_drag_offset;
      *scroll = std::max(
          0, std::min(scroll_for_thumb_top(state->diff_scrollbar_layout, thumb_top), max_scroll));
      return true;
    }
    return false;
  }

  if (state->diff_scrollbar_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->diff_scrollbar_dragging = false;
      return true;
    }
    if (m.motion == Mouse::Moved && m.button == Mouse::Left) {
      const int local_y = m.y - state->diff_scrollbar_box.y_min;
      const int thumb_top = local_y - state->diff_scrollbar_drag_offset;
      *scroll = std::max(
          0, std::min(scroll_for_thumb_top(state->diff_scrollbar_layout, thumb_top), max_scroll));
      return true;
    }
  }

  if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
    if (!in_bar) {
      return false;
    }
    const int delta = m.button == Mouse::WheelUp ? -3 : 3;
    *scroll = std::max(0, std::min(*scroll + delta, max_scroll));
    return true;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed || !in_bar) {
    return false;
  }

  trigger_press(layout_state, std::string_view(kGitDiffScrollbar));
  const int local_y = m.y - state->diff_scrollbar_box.y_min;
  if (scrollbar_thumb_hit(state->diff_scrollbar_layout, state->diff_scrollbar_box, m.x, m.y)) {
    state->diff_scrollbar_dragging = true;
    state->diff_scrollbar_drag_offset = local_y - state->diff_scrollbar_layout.thumb_y;
  } else {
    const int thumb_top = local_y - state->diff_scrollbar_layout.thumb_height / 2;
    *scroll = std::max(
        0, std::min(scroll_for_thumb_top(state->diff_scrollbar_layout, thumb_top), max_scroll));
    state->diff_scrollbar_dragging = true;
    state->diff_scrollbar_drag_offset = state->diff_scrollbar_layout.thumb_height / 2;
  }
  return true;
}

bool list_area_contains(const GitPanelState* state, int x, int y) {
  if (state == nullptr) {
    return false;
  }
  return (!state->file_list_box.IsEmpty() && state->file_list_box.Contain(x, y)) ||
         (!state->list_scrollbar_box.IsEmpty() && state->list_scrollbar_box.Contain(x, y));
}

bool handle_git_mouse(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                      FocusManagerState* focus, WorkspaceModel* workspace, Event event) {
  if (state == nullptr || layout_state == nullptr || git == nullptr || !event.is_mouse()) {
    return false;
  }

  Mouse& m = event.mouse();
  const bool is_wheel = m.button == Mouse::WheelUp || m.button == Mouse::WheelDown;
  const bool is_press = m.button == Mouse::Left && m.motion == Mouse::Pressed;


  if (state->list_sep_dragging) {
    if (m.button == Mouse::Left && m.motion == Mouse::Released) {
      state->list_sep_dragging = false;
      return true;
    }
    if (m.motion == Mouse::Moved && m.button == Mouse::Left) {
      const int panel_w =
          state->panel_box.IsEmpty() ? 120 : (state->panel_box.x_max - state->panel_box.x_min + 1);
      const int delta = m.x - state->list_sep_drag_start_x;
      state->file_list_width_custom = true;
      state->file_list_width = state->list_sep_drag_start_width + delta;
      clamp_file_list_width(state, panel_w);
      UI_WAKE(layout_state, "wake");
      return true;
    }
  }

  if (state->list_scrollbar_dragging &&
      handle_git_list_scrollbar_mouse(state, layout_state, m, state->last_list_total,
                                      state->last_list_visible)) {
    UI_WAKE(layout_state, "wake");
    return true;
  }
  if (state->diff_scrollbar_dragging &&
      handle_git_diff_scrollbar_mouse(state, layout_state, m, state->last_diff_total,
                                      state->last_diff_visible)) {
    UI_WAKE(layout_state, "wake");
    return true;
  }

  // Clic fuera del panel: soltar el buscador sin tragar el evento (el editor debe recibirlo).
  if (!state->panel_box.Contain(m.x, m.y)) {
    if (is_press && state->search_focus) {
      state->search_focus = false;
    }
    return false;
  }

  if (m.motion == Mouse::Moved) {
    const bool sep_hit =
        !state->list_sep_box.IsEmpty() && state->list_sep_box.Contain(m.x, m.y);
    if (state->list_sep_hovered != sep_hit) {
      state->list_sep_hovered = sep_hit;
    }
    update_panel_hover(
        layout_state, m.x, m.y,
        {{kGitTab0, &state->tab_boxes[0]},
         {kGitTab1, &state->tab_boxes[1]},
         {kGitTab2, &state->tab_boxes[2]},
         {kGitTab3, &state->tab_boxes[3]},
         {kGitTab4, &state->tab_boxes[4]},
         {kGitStage, &state->stage_box},
         {kGitUnstage, &state->unstage_box},
         {kGitPush, &state->push_box},
         {kGitPull, &state->pull_box},
         {kGitCommit, &state->commit_box},
         {kGitViewToggle, &state->view_toggle_box},
         {kGitSearch, &state->search_box},
         {kGitListScrollbar, &state->list_scrollbar_box},
         {kGitDiffScrollbar, &state->diff_scrollbar_box},
         {kGitCommitConfirm, &state->commit_modal_confirm_box},
         {kGitCommitCancel, &state->commit_modal_cancel_box}},
        [](std::string_view id) {
          return id.rfind("git-", 0) == 0;
        });
    return false;
  }

  if (state->commit_modal_open) {
    if (is_press) {
      if (state->commit_modal_confirm_box.Contain(m.x, m.y)) {
        trigger_press(layout_state, std::string_view(kGitCommitConfirm));
        commit_message(git, state, layout_state);
        return true;
      }
      if (state->commit_modal_cancel_box.Contain(m.x, m.y)) {
        trigger_press(layout_state, std::string_view(kGitCommitCancel));
        close_commit_modal(state, false);
        return true;
      }
    }
    return true;
  }

  // Wheel: no robar foco (evita el salto de layout al sincronizar Terminal).
  if (is_wheel) {
    if (handle_git_list_scrollbar_mouse(state, layout_state, m, state->last_list_total,
                                        state->last_list_visible)) {
      return true;
    }
    if (handle_git_diff_scrollbar_mouse(state, layout_state, m, state->last_diff_total,
                                        state->last_diff_visible)) {
      return true;
    }
    const int wheel_delta = m.button == Mouse::WheelUp ? -3 : 3;
    if (list_area_contains(state, m.x, m.y)) {
      scroll_git_list(state, wheel_delta, state->last_list_total, state->last_list_visible);
      return true;
    }
    if ((state->selected_tab == GitPanelState::kTabStatus ||
         state->selected_tab == GitPanelState::kTabTimeline ||
         state->selected_tab == GitPanelState::kTabLog ||
         state->selected_tab == GitPanelState::kTabGraph) &&
        ((!state->diff_box.IsEmpty() && state->diff_box.Contain(m.x, m.y)) ||
         (!state->diff_scrollbar_box.IsEmpty() && state->diff_scrollbar_box.Contain(m.x, m.y)))) {
      if (state->selected_tab == GitPanelState::kTabGraph) {
        const int count = static_cast<int>(git->graph_lines().size());
        scroll_git_graph(state, wheel_delta, count, state->last_diff_visible);
        return true;
      }
      if (state->selected_tab == GitPanelState::kTabStatus) {
        const int entry_index = selected_status_entry_index(git, state);
        if (entry_index >= 0) {
          const GitStatusSnapshot status = git->status();
          if (entry_index < static_cast<int>(status.entries.size())) {
            const std::string& path = status.entries[static_cast<std::size_t>(entry_index)].path;
            scroll_git_diff(state, wheel_delta, count_diff_lines(git->file_diff_text(path)),
                            state->last_diff_visible);
          }
        }
        return true;
      }
      if (state->selected_tab == GitPanelState::kTabTimeline) {
        const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
        ensure_file_timeline(git, state, active_file);
        const auto commits = git->file_timeline(active_file);
        if (state->selected_file >= 0 &&
            state->selected_file < static_cast<int>(commits.size())) {
          const std::string& hash =
              commits[static_cast<std::size_t>(state->selected_file)].hash;
          scroll_git_diff(state, wheel_delta,
                          count_diff_lines(git->timeline_diff_text(active_file, hash)),
                          state->last_diff_visible);
        }
        return true;
      }
      if (state->selected_tab == GitPanelState::kTabLog) {
        const auto commits = visible_log_commits(git, state);
        if (state->selected_file >= 0 &&
            state->selected_file < static_cast<int>(commits.size())) {
          ensure_commit_files(git, state);
          const auto& hash = commits[static_cast<std::size_t>(state->selected_file)].hash;
          const auto files = git->commit_files(hash);
          const auto lines = build_log_commit_view_lines(git, hash, files);
          scroll_git_diff(state, wheel_delta, static_cast<int>(lines.size()),
                          state->last_diff_visible);
        }
        return true;
      }
    }
    return false;
  }

  if (is_press) {
    // Clic dentro del panel pero fuera del buscador → salir del input.
    if (state->search_focus &&
        (state->search_box.IsEmpty() || !state->search_box.Contain(m.x, m.y))) {
      state->search_focus = false;
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::Terminal;
    }
  }

  if (handle_git_list_scrollbar_mouse(state, layout_state, m, state->last_list_total,
                                      state->last_list_visible)) {
    return true;
  }
  if (handle_git_diff_scrollbar_mouse(state, layout_state, m, state->last_diff_total,
                                      state->last_diff_visible)) {
    return true;
  }

  if (!is_press) {
    return false;
  }

  if (!state->list_sep_box.IsEmpty() && state->list_sep_box.Contain(m.x, m.y)) {
    state->list_sep_dragging = true;
    state->file_list_width_custom = true;
    state->list_sep_drag_start_x = m.x;
    state->list_sep_drag_start_width = state->file_list_width;
    return true;
  }

  if (state->tab_boxes[0].Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitTab0));
    select_tab(state, git, GitPanelState::kTabStatus);
    return true;
  }
  if (state->tab_boxes[1].Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitTab1));
    select_tab(state, git, GitPanelState::kTabLog);
    return true;
  }
  if (state->tab_boxes[2].Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitTab2));
    select_tab(state, git, GitPanelState::kTabBranches);
    return true;
  }
  if (state->tab_boxes[3].Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitTab3));
    select_tab(state, git, GitPanelState::kTabTimeline);
    return true;
  }
  if (state->tab_boxes[4].Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitTab4));
    select_tab(state, git, GitPanelState::kTabGraph);
    return true;
  }

  if ((state->selected_tab == GitPanelState::kTabStatus ||
       state->selected_tab == GitPanelState::kTabLog) &&
      state->search_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitSearch));
    state->search_focus = true;
    cursor_blink::show();
    return true;
  }

  if (state->selected_tab == GitPanelState::kTabStatus) {
    if (state->view_toggle_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitViewToggle));
      state->tree_view = !state->tree_view;
      state->file_scroll = 0;
      return true;
    }
    if (state->stage_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitStage));
      stage_selected(git, state, layout_state);
      return true;
    }
    if (state->unstage_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitUnstage));
      unstage_selected(git, state, layout_state);
      return true;
    }
    if (state->push_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitPush));
      state->operation_pending = true;
      git->push([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? i18n::tr("git.status.push_ok") : msg);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
      });
      return true;
    }
    if (state->pull_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitPull));
      state->operation_pending = true;
      git->pull([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? i18n::tr("git.status.pull_ok") : msg);
        if (layout_state != nullptr) {
          UI_WAKE(layout_state, "wake");
        }
      });
      return true;
    }
    if (state->commit_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitCommit));
      open_commit_modal(state);
      return true;
    }
  }

  if (list_area_contains(state, m.x, m.y) && state->selected_tab != GitPanelState::kTabGraph) {
    const auto local = local_row_in_box(state->file_list_box, m.x, m.y);
    if (!local.has_value()) {
      return true;
    }
    const int index = list_index_from_mouse(state, *local);
    if (state->selected_tab == GitPanelState::kTabStatus) {
      const GitStatusSnapshot status = git->status();
      const auto& entries = status.entries;
      const auto indices = visible_status_indices(entries, state->search_query);
      const auto rows = build_status_visual_rows(entries, indices, state->tree_view);
      if (index < 0 || index >= static_cast<int>(rows.size())) {
        return true;
      }
      const auto& row = rows[static_cast<std::size_t>(index)];
      const bool ctrl = mouse_control_active(m, event);

      if (row.is_dir) {
        const auto kids = entry_indices_under_prefix(entries, indices, row.dir_prefix);
        set_multi_entries(state, kids, ctrl);
        if (!kids.empty()) {
          int selected_pos = 0;
          for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
            if (indices[static_cast<std::size_t>(i)] == kids.front()) {
              selected_pos = i;
              break;
            }
          }
          state->selected_file = selected_pos;
          scroll_status_selection_into_view(state, rows, kids.front(), state->last_list_visible);
          ensure_selected_diff(git, state);
        }
        return true;
      }

      if (row.entry_index < 0) {
        return true;
      }
      int selected_pos = 0;
      for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
        if (indices[static_cast<std::size_t>(i)] == row.entry_index) {
          selected_pos = i;
          break;
        }
      }
      if (ctrl) {
        toggle_multi_entry(state, row.entry_index);
        state->selected_file = selected_pos;
        scroll_status_selection_into_view(state, rows, row.entry_index, state->last_list_visible);
        ensure_selected_diff(git, state);
        return true;
      }

      const bool open_diff =
          is_double_click(row.entry_index, &state->last_file_click_index, &state->last_file_click_ms);
      clear_multi_selection(state);
      state->selected_file = selected_pos;
      scroll_status_selection_into_view(state, rows, row.entry_index, state->last_list_visible);
      ensure_selected_diff(git, state);
      if (open_diff) {
        open_status_file_diff(git, state, layout_state, focus, workspace, row.entry_index);
      }
      return true;
    }

    int count = 0;
    if (state->selected_tab == GitPanelState::kTabLog) {
      count = static_cast<int>(visible_log_commits(git, state).size());
    } else if (state->selected_tab == GitPanelState::kTabTimeline) {
      const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
      count = static_cast<int>(git->file_timeline(active_file).size());
    } else {
      count = static_cast<int>(git->branches().size());
    }
    if (index >= 0 && index < count) {
      state->selected_file = index;
      scroll_selection_into_view(state, count, state->last_list_visible);
      if (state->selected_tab == GitPanelState::kTabLog) {
        ensure_commit_files(git, state);
      } else if (state->selected_tab == GitPanelState::kTabTimeline) {
        const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
        ensure_file_timeline(git, state, active_file);
      }
    }
    return true;
  }

  return false;
}

}  // namespace

void GitPanelEnsureSelectedDiff(GitService* git, GitPanelState* state) {
  ensure_selected_diff(git, state);
}

void GitPanelActivate(GitService* git, GitPanelState* state) {
  if (state == nullptr) {
    return;
  }
  state->selected_tab = GitPanelState::kTabStatus;
  state->selected_file = 0;
  state->file_scroll = 0;
  state->log_scroll = 0;
  state->branch_scroll = 0;
  state->timeline_scroll = 0;
  state->graph_scroll = 0;
  state->diff_scroll = 0;
  state->last_diff_path.clear();
  state->last_timeline_commit.clear();
  state->last_log_commit.clear();
  state->timeline_file.clear();
  state->commit_modal_open = false;
  state->search_focus = false;
  clear_multi_selection(state);
  if (git != nullptr) {
    git->refresh_status();
    GitPanelEnsureSelectedDiff(git, state);
  }
}

Component MakeGitPanel(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                       FocusManagerState* focus, WorkspaceModel* workspace, int* content_height) {
  auto dispatch_keys = [git, state, layout_state, focus, workspace](Event event) {
    return handle_git_keys(git, state, layout_state, focus, workspace, event);
  };
  auto dispatch_mouse = [git, state, layout_state, focus, workspace](Event event) {
    return handle_git_mouse(git, state, layout_state, focus, workspace, event);
  };

  if (layout_state != nullptr) {
    layout_state->git_key_handler = dispatch_keys;
    layout_state->git_mouse_handler = dispatch_mouse;
  }

  auto panel = Renderer([git, state, layout_state, workspace, content_height] {
    if (git == nullptr || state == nullptr) {
      return text(i18n::tr("git.unavailable")) | color(theme::Muted()) | flex;
    }

    const int body_height =
        content_height != nullptr && *content_height > 0 ? *content_height : 18;
    // Clamp duro: un valor absurdo aquí puede hacer bad_alloc al construir la scrollbar.
    constexpr int kMaxVisible = 256;
    const int list_visible = std::max(1, std::min(kMaxVisible, body_height - 2));
    const int diff_visible = std::max(1, std::min(kMaxVisible, body_height - 4));
    state->last_list_visible = list_visible;
    state->last_diff_visible = diff_visible;


    // Evitar context_repo_info() en cada paint: hace run_git síncrono y satura el loop.
    const GitRepoInfo repo = git->repo_info();
    const GitRepoInfo main_repo = repo;
    if (!repo.valid) {
      return vbox({
                 text(" " + (repo.last_error.empty() ? i18n::tr("git.not_repo")
                                                    : repo.last_error)) |
                     color(theme::Muted()),
             }) |
             flex | bgcolor(theme::PanelBg()) | reflect(state->panel_box);
    }

    std::ostringstream branch_text;
    branch_text << repo.branch;
    if (main_repo.subrepo_count > 0) {
      branch_text << "  (" << i18n::tr_fmt("git.header.subrepos",
                                            {std::to_string(main_repo.subrepo_count)}) << ")";
    }

    const bool hover0 = interaction_active(layout_state, kGitTab0);
    const bool hover1 = interaction_active(layout_state, kGitTab1);
    const bool hover2 = interaction_active(layout_state, kGitTab2);
    const bool hover3 = interaction_active(layout_state, kGitTab3);
    const bool hover4 = interaction_active(layout_state, kGitTab4);
    const bool press0 =
        layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab0));
    const bool press1 =
        layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab1));
    const bool press2 =
        layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab2));
    const bool press3 =
        layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab3));
    const bool press4 =
        layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab4));

    Elements header_left;
    header_left.push_back(text(branch_text.str()) | bold | color(theme::Success()));

    const bool show_search = state->selected_tab == GitPanelState::kTabStatus ||
                             state->selected_tab == GitPanelState::kTabLog;
    if (state->selected_tab == GitPanelState::kTabStatus) {
      const bool view_hovered = interaction_active(layout_state, kGitViewToggle);
      const bool view_pressed =
          layout_state != nullptr &&
          layout_state->clickable.is_pressed(std::string_view(kGitViewToggle));
      const std::string view_glyph = state->tree_view ? "☰" : "⊞";
      header_left.push_back(text(" "));
      header_left.push_back(MakeIconButton(view_glyph, view_hovered, view_pressed,
                                            &state->view_toggle_box));
    }

    if (show_search) {
      header_left.push_back(text(" "));
      Element search_line =
          hbox({text(" /"),
                RenderBlinkInputLine(state->search_query, state->search_cursor, state->search_focus),
                text(" ")}) |
          bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1) | size(WIDTH, EQUAL, 22) |
          reflect(state->search_box);
      header_left.push_back(std::move(search_line));
    }

    if (state->selected_tab == GitPanelState::kTabStatus) {
      const bool pull_h = interaction_active(layout_state, kGitPull);
      const bool push_h = interaction_active(layout_state, kGitPush);
      const bool stage_h = interaction_active(layout_state, kGitStage);
      const bool unstage_h = interaction_active(layout_state, kGitUnstage);
      const bool commit_h = interaction_active(layout_state, kGitCommit);
      const bool pull_p =
          layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitPull));
      const bool push_p =
          layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitPush));
      const bool stage_p =
          layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitStage));
      const bool unstage_p = layout_state != nullptr &&
                             layout_state->clickable.is_pressed(std::string_view(kGitUnstage));
      const bool commit_p = layout_state != nullptr &&
                            layout_state->clickable.is_pressed(std::string_view(kGitCommit));
      header_left.push_back(text(" "));
      header_left.push_back(MakeIconButton("↓", pull_h, pull_p, &state->pull_box));
      header_left.push_back(MakeIconButton("↑", push_h, push_p, &state->push_box));
      header_left.push_back(MakeIconButton("+", stage_h, stage_p, &state->stage_box));
      header_left.push_back(MakeIconButton("−", unstage_h, unstage_p, &state->unstage_box));
      header_left.push_back(MakeIconButton("✓", commit_h, commit_p, &state->commit_box));
    }

    if (!state->status_message.empty()) {
      header_left.push_back(text("  " + state->status_message) | color(theme::Warning()));
    }

    Element tabs = hbox({
        hbox(std::move(header_left)),
        filler(),
        MakeCompactTabButton(i18n::tr("git.tab.status"),
                             state->selected_tab == GitPanelState::kTabStatus, hover0, press0,
                             &state->tab_boxes[0]),
        MakeCompactTabButton(i18n::tr("git.tab.log"), state->selected_tab == GitPanelState::kTabLog,
                             hover1, press1, &state->tab_boxes[1]),
        MakeCompactTabButton(i18n::tr("git.tab.branches"),
                             state->selected_tab == GitPanelState::kTabBranches, hover2, press2,
                             &state->tab_boxes[2]),
        MakeCompactTabButton(i18n::tr("git.tab.timeline"),
                             state->selected_tab == GitPanelState::kTabTimeline, hover3, press3,
                             &state->tab_boxes[3]),
        MakeCompactTabButton(i18n::tr("git.tab.graph"),
                             state->selected_tab == GitPanelState::kTabGraph, hover4, press4,
                             &state->tab_boxes[4]),
        state->operation_pending ? text(" " + std::string(spinner::glyph())) | color(theme::Accent())
                                 : text(""),
    });

    Elements left_rows;
    Elements center_rows;
    const GitStatusSnapshot status = git->status();
    int list_total = 0;

    if (state->selected_tab == GitPanelState::kTabStatus) {
      const auto& entries = status.entries;
      const auto indices = visible_status_indices(entries, state->search_query);
      clamp_selection(state, static_cast<int>(indices.size()));
      const auto rows = build_status_visual_rows(entries, indices, state->tree_view);
      list_total = static_cast<int>(rows.size());
      if (rows.empty()) {
        left_rows.push_back(text(i18n::tr("git.working_tree_clean")) | color(theme::Muted()));
      }
      const int selected_entry =
          indices.empty() ? -1 : indices[static_cast<std::size_t>(state->selected_file)];
      // No mover el scroll en render: provoca feedback layout → bad_alloc/flickeo.
      clamp_list_scroll(state, list_total, list_visible);
      const int end = std::min(list_total, state->file_scroll + list_visible);
      for (int i = state->file_scroll; i < end; ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        if (row.is_dir) {
          const bool selected = dir_row_selected(row, entries, indices, state->multi_selected);
          left_rows.push_back(render_status_file_row(GitStatusEntry{}, row, selected));
          continue;
        }
        const auto& entry = entries[static_cast<std::size_t>(row.entry_index)];
        const bool selected = row.entry_index == selected_entry ||
                              state->multi_selected.count(row.entry_index) > 0;
        left_rows.push_back(render_status_file_row(entry, row, selected));
      }

      if (selected_entry >= 0) {
        const std::string& path = entries[static_cast<std::size_t>(selected_entry)].path;
        center_rows.push_back(text(i18n::tr_fmt("git.diff.title", {path})) | color(theme::Accent()) |
                              bold);
        center_rows.push_back(separator() | color(theme::AccentDim()));
        const std::string diff = git->file_diff_text(path);
        const bool loading = diff.empty() && git->busy();
        const int diff_lines = count_diff_lines(diff);
        clamp_diff_scroll(state, diff_lines, diff_visible);
        state->last_diff_total = diff_lines;
        center_rows.push_back(render_diff_lines(diff, loading, state->diff_scroll, diff_visible) |
                              flex);
      } else {
        state->last_diff_total = 0;
        center_rows.push_back(text(i18n::tr("git.select_file")) | color(theme::Muted()));
      }
    } else if (state->selected_tab == GitPanelState::kTabLog) {
      const auto commits = visible_log_commits(git, state);
      list_total = static_cast<int>(commits.size());
      const bool search_loading =
          !state->log_search_applied.empty() && !git->log_search_ready(state->log_search_applied);
      clamp_selection(state, list_total);
      clamp_list_scroll(state, list_total, list_visible);
      const int end = std::min(list_total, state->log_scroll + list_visible);
      for (int i = state->log_scroll; i < end; ++i) {
        const auto& entry = commits[static_cast<std::size_t>(i)];
        const bool selected = i == state->selected_file;
        Element row = text(" " + entry.short_hash + " " + entry.message) | color(theme::Header());
        left_rows.push_back(StyleListRow(std::move(row), selected, false, false));
      }
      if (search_loading) {
        left_rows.push_back(text(i18n::tr("git.diff.loading")) | color(theme::Muted()));
      } else if (commits.empty()) {
        left_rows.push_back(text(state->log_search_applied.empty() ? i18n::tr("git.log.empty")
                                                                 : i18n::tr("git.log.search_empty")) |
                            color(theme::Muted()));
      }

      ensure_commit_files(git, state);
      if (!commits.empty() && state->selected_file >= 0 &&
          state->selected_file < static_cast<int>(commits.size())) {
        const auto& entry = commits[static_cast<std::size_t>(state->selected_file)];
        center_rows.push_back(text(entry.short_hash + " " + entry.message) | color(theme::Accent()) |
                              bold);
        center_rows.push_back(separator() | color(theme::AccentDim()));
        const auto files = git->commit_files(entry.hash);
        const bool files_loading = files.empty() && git->busy() && !git->has_commit_files(entry.hash);
        if (files_loading) {
          state->last_diff_total = 0;
          center_rows.push_back(text(i18n::tr("git.diff.loading")) | color(theme::Muted()));
        } else if (files.empty()) {
          state->last_diff_total = 0;
          center_rows.push_back(text(i18n::tr("git.log.no_files")) | color(theme::Muted()));
        } else {
          const auto lines = build_log_commit_view_lines(git, entry.hash, files);
          clamp_diff_scroll(state, static_cast<int>(lines.size()), diff_visible);
          state->last_diff_total = static_cast<int>(lines.size());
          center_rows.push_back(
              render_log_commit_view(lines, state->diff_scroll, diff_visible) | flex);
        }
      } else {
        state->last_diff_total = 0;
        center_rows.push_back(text(i18n::tr("git.log.title")) | color(theme::Accent()) | bold);
        center_rows.push_back(text(i18n::tr("git.log.navigate_hint")) | color(theme::Muted()));
      }
    } else if (state->selected_tab == GitPanelState::kTabBranches) {
      state->last_diff_total = 0;
      const auto branches = git->branches();
      list_total = static_cast<int>(branches.size());
      clamp_selection(state, list_total);
      clamp_list_scroll(state, list_total, list_visible);
      const int end = std::min(list_total, state->branch_scroll + list_visible);
      for (int i = state->branch_scroll; i < end; ++i) {
        const auto& branch = branches[static_cast<std::size_t>(i)];
        const bool selected = i == state->selected_file;
        Color branch_color = branch.current ? theme::Success() : theme::Header();
        const std::string prefix = branch.current ? "* " : "  ";
        Element row = text(" " + prefix + branch.name) | color(branch_color);
        left_rows.push_back(StyleListRow(std::move(row), selected, false, false));
      }
      if (branches.empty()) {
        left_rows.push_back(text(i18n::tr("git.branches.empty")) | color(theme::Muted()));
      }
      center_rows.push_back(text(i18n::tr("git.branches.title")) | color(theme::Accent()) | bold);
      center_rows.push_back(text(i18n::tr("git.branches.switch_hint")) | color(theme::Muted()));
    } else if (state->selected_tab == GitPanelState::kTabTimeline) {
      const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
      ensure_file_timeline(git, state, active_file);
      if (active_file.empty()) {
        state->last_diff_total = 0;
        left_rows.push_back(text(i18n::tr("git.timeline.no_file")) | color(theme::Muted()));
        center_rows.push_back(text(i18n::tr("git.timeline.title")) | color(theme::Accent()) | bold);
        center_rows.push_back(text(i18n::tr("git.timeline.no_file_hint")) | color(theme::Muted()));
      } else {
        const auto commits = git->file_timeline(active_file);
        list_total = static_cast<int>(commits.size());
        clamp_selection(state, list_total);
        clamp_list_scroll(state, list_total, list_visible);
        const int end = std::min(list_total, state->timeline_scroll + list_visible);
        for (int i = state->timeline_scroll; i < end; ++i) {
          const auto& entry = commits[static_cast<std::size_t>(i)];
          const bool selected = i == state->selected_file;
          Element row = text(" " + entry.short_hash + " " + entry.message) | color(theme::Header());
          left_rows.push_back(StyleListRow(std::move(row), selected, false, false));
        }
        if (commits.empty()) {
          const bool loading = git->busy();
          left_rows.push_back(text(loading ? i18n::tr("git.diff.loading")
                                           : i18n::tr("git.timeline.empty")) |
                              color(theme::Muted()));
        }

        center_rows.push_back(text(i18n::tr_fmt("git.timeline.file_title", {active_file})) |
                              color(theme::Accent()) | bold);
        if (!commits.empty() && state->selected_file >= 0 &&
            state->selected_file < static_cast<int>(commits.size())) {
          const auto& entry = commits[static_cast<std::size_t>(state->selected_file)];
          center_rows.push_back(text(entry.short_hash + " " + entry.message) | color(theme::Header()));
          center_rows.push_back(text(entry.author + " · " + entry.date) | color(theme::Muted()));
          center_rows.push_back(separator() | color(theme::AccentDim()));
          const std::string diff = git->timeline_diff_text(active_file, entry.hash);
          const bool loading = diff.empty() && git->busy();
          const int diff_lines = count_diff_lines(diff);
          clamp_diff_scroll(state, diff_lines, diff_visible);
          state->last_diff_total = diff_lines;
          center_rows.push_back(
              render_diff_lines(diff, loading, state->diff_scroll, diff_visible) | flex);
        } else if (!commits.empty()) {
          state->last_diff_total = 0;
          center_rows.push_back(text(i18n::tr("git.timeline.select_commit")) | color(theme::Muted()));
        }
      }
    } else {
      git->refresh_graph();
      left_rows.push_back(text(i18n::tr("git.graph.title")) | color(theme::Accent()) | bold);
      left_rows.push_back(text(i18n::tr("git.graph.scroll_hint")) | color(theme::Muted()));
      left_rows.push_back(text(""));
      left_rows.push_back(text(i18n::tr("git.graph.legend")) | color(theme::Accent()) | bold);
      left_rows.push_back(text(i18n::tr("git.graph.legend_branches")) | color(theme::Success()));
      left_rows.push_back(text(i18n::tr("git.graph.legend_graph")) | color(theme::Accent()));
      const auto lines = git->graph_lines();
      center_rows.push_back(text(i18n::tr("git.graph.title")) | color(theme::Accent()) | bold);
      center_rows.push_back(separator() | color(theme::AccentDim()));
      if (lines.empty()) {
        state->last_diff_total = 0;
        const bool loading = !git->graph_loaded() && git->busy();
        center_rows.push_back(text(loading ? i18n::tr("git.diff.loading") : i18n::tr("git.graph.empty")) |
                              color(theme::Muted()));
      } else {
        state->last_diff_total = static_cast<int>(lines.size());
        const int end = std::min(static_cast<int>(lines.size()), state->graph_scroll + diff_visible);
        state->graph_scroll = std::max(
            0, std::min(state->graph_scroll, std::max(0, static_cast<int>(lines.size()) - diff_visible)));
        for (int i = state->graph_scroll; i < end; ++i) {
          center_rows.push_back(render_graph_line(lines[static_cast<std::size_t>(i)]));
        }
      }
    }

    // flex hace que la hitbox ocupe toda la altura; no rellenar con N filas vacías.
    const int panel_w =
        state->panel_box.IsEmpty() ? 120 : (state->panel_box.x_max - state->panel_box.x_min + 1);
    clamp_file_list_width(state, panel_w);

    state->last_list_total = list_total;
    const int rendered_lines = std::max(1, list_visible);
    state->list_scrollbar_layout =
        compute_scrollbar_layout(list_total, list_scroll_offset(state), list_visible, rendered_lines);
    const bool scrollbar_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(std::string_view(kGitListScrollbar));
    const bool scrollbar_active =
        state->list_scrollbar_dragging ||
        (layout_state != nullptr &&
         layout_state->clickable.is_pressed(std::string_view(kGitListScrollbar)));

    const int diff_scroll_pos =
        state->selected_tab == GitPanelState::kTabGraph ? state->graph_scroll : state->diff_scroll;
    state->diff_scrollbar_layout = compute_scrollbar_layout(
        state->last_diff_total, diff_scroll_pos, diff_visible, std::max(1, diff_visible));
    const bool diff_scrollbar_hovered =
        layout_state != nullptr &&
        layout_state->clickable.is_hovered(std::string_view(kGitDiffScrollbar));
    const bool diff_scrollbar_active =
        state->diff_scrollbar_dragging ||
        (layout_state != nullptr &&
         layout_state->clickable.is_pressed(std::string_view(kGitDiffScrollbar)));

    Element list = vbox(std::move(left_rows)) | reflect(state->file_list_box) | flex |
                   bgcolor(theme::CodeBg());
    Element scrollbar =
        vertical_scrollbar(list_total, list_scroll_offset(state), list_visible, rendered_lines,
                           scrollbar_hovered, scrollbar_active) |
        reflect(state->list_scrollbar_box);

    Element diff_pane = vbox(std::move(center_rows)) | reflect(state->diff_box) | flex |
                        bgcolor(theme::CodeBg());
    Element diff_scrollbar =
        vertical_scrollbar(state->last_diff_total, diff_scroll_pos, diff_visible,
                           std::max(1, diff_visible), diff_scrollbar_hovered, diff_scrollbar_active) |
        reflect(state->diff_scrollbar_box);

    Element body = hbox({
        hbox({std::move(list) | flex, std::move(scrollbar)}) |
            size(WIDTH, EQUAL, state->file_list_width) | bgcolor(theme::CodeBg()) | border,
        SplitSeparatorVertical(state->list_sep_hovered, state->list_sep_dragging,
                               &state->list_sep_box),
        hbox({std::move(diff_pane) | flex, std::move(diff_scrollbar)}) | flex |
            bgcolor(theme::CodeBg()) | border,
    });

    Element result = vbox({
                         tabs | size(HEIGHT, EQUAL, 1),
                         separator(),
                         std::move(body) | flex,
                     }) |
                     flex | bgcolor(theme::PanelBg()) | reflect(state->panel_box);

    if (state->commit_modal_open) {
      result = ScreenModalOverlay(std::move(result), render_commit_modal(state, layout_state));
    }
    return result;
  });

  return WrapFocusable(CatchEvent(panel, [dispatch_keys](Event event) {
    // El ratón lo gestiona solo git_mouse_handler (application); duplicarlo aquí
    // reentra en hover/scroll y puede disparar wakes en bucle.
    if (event.is_mouse()) {
      return false;
    }
    return dispatch_keys(event);
  }));
}

}  // namespace tuide
