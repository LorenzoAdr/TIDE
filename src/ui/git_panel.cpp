#include "ui/git_panel.hpp"
#include "ui/ui_wake.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include "app/workspace_model.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/focusable_component.hpp"
#include "ui/key_bindings.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/spinner.hpp"
#include "ui/text_input_style.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"

namespace tuide {

using namespace ftxui;

namespace {

constexpr int kFileListWidth = 28;
constexpr int kActionWidth = 26;

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
constexpr const char* kGitLogSearch = "git-log-search";

std::string status_badge(const GitStatusEntry& entry) {
  std::string badge;
  if (entry.staged != GitFileStatus::kUnmodified) {
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
  if (entry.unstaged == GitFileStatus::kUntracked) {
    return theme::Warning();
  }
  if (entry.staged != GitFileStatus::kUnmodified) {
    return theme::Success();
  }
  return theme::Accent();
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
                           FocusManagerState* focus, WorkspaceModel* workspace, int index) {
  if (git == nullptr || state == nullptr || layout_state == nullptr || workspace == nullptr) {
    return;
  }
  const auto entries = git->status().entries;
  if (index < 0 || index >= static_cast<int>(entries.size())) {
    return;
  }
  if (layout_state->git_open_diff_view) {
    layout_state->git_open_diff_view(entries[static_cast<std::size_t>(index)].path);
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
  state->log_search_applied = state->log_search_query;
  state->selected_file = 0;
  state->log_scroll = 0;
  state->last_log_commit.clear();
  state->diff_scroll = 0;
  if (!state->log_search_applied.empty()) {
    git->refresh_log_search(state->log_search_applied);
  }
}

void clamp_selection(GitPanelState* state, int count) {
  if (state == nullptr || count <= 0) {
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

void ensure_selected_diff(GitService* git, GitPanelState* state) {
  if (git == nullptr || state == nullptr || state->selected_tab != GitPanelState::kTabStatus) {
    return;
  }
  const auto entries = git->status().entries;
  clamp_selection(state, static_cast<int>(entries.size()));
  if (entries.empty()) {
    return;
  }
  const std::string& path = entries[static_cast<std::size_t>(state->selected_file)].path;
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
  } else if (git != nullptr) {
    git->refresh_status();
    ensure_selected_diff(git, state);
  }
}

void stage_selected(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  const auto entries = git->status().entries;
  clamp_selection(state, static_cast<int>(entries.size()));
  if (state->selected_file < 0 || state->selected_file >= static_cast<int>(entries.size())) {
    return;
  }
  const std::string path = entries[static_cast<std::size_t>(state->selected_file)].path;
  state->operation_pending = true;
  git->stage_file(path, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    set_status(state, ok ? i18n::tr("git.status.staged") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

void unstage_selected(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  const auto entries = git->status().entries;
  clamp_selection(state, static_cast<int>(entries.size()));
  if (state->selected_file < 0 || state->selected_file >= static_cast<int>(entries.size())) {
    return;
  }
  const std::string path = entries[static_cast<std::size_t>(state->selected_file)].path;
  state->operation_pending = true;
  git->unstage_file(path, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    set_status(state, ok ? i18n::tr("git.status.unstaged") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

void commit_message(GitService* git, GitPanelState* state, MainLayoutState* layout_state) {
  if (state->commit_message.empty()) {
    return;
  }
  state->operation_pending = true;
  const std::string message = state->commit_message;
  git->commit(message, [state, layout_state](bool ok, const std::string& msg) {
    state->operation_pending = false;
    if (ok) {
      state->commit_message.clear();
      state->commit_cursor = 0;
    }
    set_status(state, ok ? i18n::tr("git.status.commit_ok") : msg);
    if (layout_state != nullptr) {
      UI_WAKE(layout_state, "wake");
    }
  });
}

bool handle_git_keys(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                     FocusManagerState* focus, WorkspaceModel* workspace, Event event) {
  if (state == nullptr || git == nullptr || !git->is_repo()) {
    return false;
  }
  if (focus != nullptr && focus->region != FocusRegion::Terminal) {
    return false;
  }

  if (event_is_tuide_global_shortcut(event)) {
    return false;
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
    const int count = static_cast<int>(git->status().entries.size());
    const int visible = state->last_list_visible;
    if ((event == Event::ArrowUp || event == Event::Character('k')) && state->selected_file > 0) {
      --state->selected_file;
      scroll_selection_into_view(state, count, visible);
      ensure_selected_diff(git, state);
      return true;
    }
    if ((event == Event::ArrowDown || event == Event::Character('j')) && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      scroll_selection_into_view(state, count, visible);
      ensure_selected_diff(git, state);
      return true;
    }
    if (!state->commit_input_focus) {
      if (event == Event::Return && count > 0) {
        open_status_file_diff(git, state, layout_state, focus, workspace, state->selected_file);
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
    }
    if (state->commit_input_focus) {
      cursor_blink::show();
      if (event == Event::Return) {
        commit_message(git, state, layout_state);
        return true;
      }
      if (event == Event::Escape) {
        state->commit_input_focus = false;
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
      if (event.is_character()) {
        const std::string ch = event.character();
        if (!ch.empty() && ch[0] >= 32) {
          state->commit_message.insert(static_cast<std::size_t>(state->commit_cursor), ch);
          state->commit_cursor += static_cast<int>(ch.size());
        }
        return true;
      }
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
      state->commit_input_focus = true;
      cursor_blink::show();
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabLog) {
    if (state->log_search_focus) {
      cursor_blink::show();
      if (event == Event::Return) {
        apply_log_search(git, state);
        return true;
      }
      if (event == Event::Escape) {
        state->log_search_focus = false;
        return true;
      }
      if (event == Event::Backspace) {
        if (state->log_search_cursor > 0 &&
            state->log_search_cursor <= static_cast<int>(state->log_search_query.size())) {
          state->log_search_query.erase(static_cast<std::size_t>(state->log_search_cursor - 1), 1);
          --state->log_search_cursor;
        }
        return true;
      }
      if (event.is_character()) {
        const std::string ch = event.character();
        if (!ch.empty() && ch[0] >= 32) {
          state->log_search_query.insert(static_cast<std::size_t>(state->log_search_cursor), ch);
          state->log_search_cursor += static_cast<int>(ch.size());
        }
        return true;
      }
      return true;
    }
    if (event == Event::Character('/')) {
      state->log_search_focus = true;
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
    if (state->commit_input_focus) {
      state->commit_input_focus = false;
      return true;
    }
    if (state->log_search_focus) {
      state->log_search_focus = false;
      return true;
    }
    return false;
  }

  return false;
}

bool handle_git_mouse(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                      FocusManagerState* focus, WorkspaceModel* workspace, Event event) {
  if (state == nullptr || layout_state == nullptr || git == nullptr || !event.is_mouse()) {
    return false;
  }

  Mouse& m = event.mouse();
  if (m.motion == Mouse::Moved) {
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
         {kGitLogSearch, &state->log_search_box}},
        [](std::string_view id) {
          return id.rfind("git-", 0) == 0;
        });
    return false;
  }

  if (!state->panel_box.Contain(m.x, m.y)) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = FocusRegion::Terminal;
  }

  if (m.button == Mouse::WheelUp || m.button == Mouse::WheelDown) {
    const int wheel_delta = m.button == Mouse::WheelUp ? -3 : 3;
    if (state->file_list_box.Contain(m.x, m.y)) {
      int count = 0;
      if (state->selected_tab == GitPanelState::kTabStatus) {
        count = static_cast<int>(git->status().entries.size());
      } else if (state->selected_tab == GitPanelState::kTabLog) {
        count = static_cast<int>(visible_log_commits(git, state).size());
      } else if (state->selected_tab == GitPanelState::kTabTimeline) {
        const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
        count = static_cast<int>(git->file_timeline(active_file).size());
      } else {
        count = static_cast<int>(git->branches().size());
      }
      scroll_git_list(state, wheel_delta, count, state->last_list_visible);
      return true;
    }
    if ((state->selected_tab == GitPanelState::kTabStatus ||
         state->selected_tab == GitPanelState::kTabTimeline) &&
        state->diff_box.Contain(m.x, m.y)) {
      std::string diff;
      if (state->selected_tab == GitPanelState::kTabStatus) {
        const auto& entries = git->status().entries;
        if (state->selected_file >= 0 && state->selected_file < static_cast<int>(entries.size())) {
          diff = git->file_diff_text(entries[static_cast<std::size_t>(state->selected_file)].path);
        }
      } else {
        const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
        ensure_file_timeline(git, state, active_file);
        const auto commits = git->file_timeline(active_file);
        if (state->selected_file >= 0 &&
            state->selected_file < static_cast<int>(commits.size())) {
          const std::string& hash =
              commits[static_cast<std::size_t>(state->selected_file)].hash;
          diff = git->timeline_diff_text(active_file, hash);
        }
      }
      if (!diff.empty()) {
        scroll_git_diff(state, wheel_delta, count_diff_lines(diff), state->last_diff_visible);
      }
      return true;
    }
    if (state->selected_tab == GitPanelState::kTabLog && state->diff_box.Contain(m.x, m.y)) {
      const auto commits = visible_log_commits(git, state);
      if (state->selected_file >= 0 &&
          state->selected_file < static_cast<int>(commits.size())) {
        ensure_commit_files(git, state);
        const auto& hash = commits[static_cast<std::size_t>(state->selected_file)].hash;
        const auto files = git->commit_files(hash);
        const auto lines = build_log_commit_view_lines(git, hash, files);
        scroll_git_diff(state, wheel_delta, static_cast<int>(lines.size()), state->last_diff_visible);
      }
      return true;
    }
    if (state->selected_tab == GitPanelState::kTabGraph && state->diff_box.Contain(m.x, m.y)) {
      const int count = static_cast<int>(git->graph_lines().size());
      scroll_git_graph(state, wheel_delta, count, state->last_diff_visible);
      return true;
    }
    return false;
  }

  if (m.button != Mouse::Left || m.motion != Mouse::Pressed) {
    return false;
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

  if (state->file_list_box.Contain(m.x, m.y) &&
      state->selected_tab != GitPanelState::kTabGraph) {
    const auto local = local_row_in_box(state->file_list_box, m.x, m.y);
    if (!local.has_value()) {
      return true;
    }
    const int index = list_index_from_mouse(state, *local);
    int count = 0;
    if (state->selected_tab == GitPanelState::kTabStatus) {
      count = static_cast<int>(git->status().entries.size());
    } else if (state->selected_tab == GitPanelState::kTabLog) {
      count = static_cast<int>(visible_log_commits(git, state).size());
    } else if (state->selected_tab == GitPanelState::kTabTimeline) {
      const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
      count = static_cast<int>(git->file_timeline(active_file).size());
    } else {
      count = static_cast<int>(git->branches().size());
    }
    if (index >= 0 && index < count) {
      const bool open_diff =
          state->selected_tab == GitPanelState::kTabStatus &&
          is_double_click(index, &state->last_file_click_index, &state->last_file_click_ms);
      state->selected_file = index;
      scroll_selection_into_view(state, count, state->last_list_visible);
      if (state->selected_tab == GitPanelState::kTabStatus) {
        ensure_selected_diff(git, state);
        if (open_diff) {
          open_status_file_diff(git, state, layout_state, focus, workspace, index);
        }
      } else if (state->selected_tab == GitPanelState::kTabLog) {
        ensure_commit_files(git, state);
      } else if (state->selected_tab == GitPanelState::kTabTimeline) {
        const std::string active_file = workspace != nullptr ? workspace->active_file : std::string{};
        ensure_file_timeline(git, state, active_file);
      }
    }
    return true;
  }

  if (state->selected_tab == GitPanelState::kTabLog &&
      state->log_search_box.Contain(m.x, m.y)) {
    trigger_press(layout_state, std::string_view(kGitLogSearch));
    state->log_search_focus = true;
    cursor_blink::show();
    return true;
  }

  if (state->selected_tab == GitPanelState::kTabStatus) {
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
      state->commit_input_focus = true;
      cursor_blink::show();
      return true;
    }
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
  state->commit_input_focus = false;
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
    const int list_visible = std::max(1, body_height - 2);
    const int diff_visible = std::max(1, body_height - 4);
    state->last_list_visible = list_visible;
    state->last_diff_visible = diff_visible;

    const GitRepoInfo repo = git->has_subrepos() ? git->context_repo_info() : git->repo_info();
    const GitRepoInfo main_repo = git->repo_info();
    if (!repo.valid) {
      return vbox({
                 text(" " + (repo.last_error.empty() ? i18n::tr("git.not_repo")
                                                    : repo.last_error)) |
                     color(theme::Muted()),
             }) |
             flex | bgcolor(theme::PanelBg()) | reflect(state->panel_box);
    }

    std::ostringstream header;
    header << repo.branch;
    if (main_repo.subrepo_count > 0) {
      header << "  (" << i18n::tr_fmt("git.header.subrepos",
                                       {std::to_string(main_repo.subrepo_count)}) << ")";
    }
    const GitStatusSnapshot status = git->status();
    if (status.staged_count > 0 || status.unstaged_count > 0 || status.untracked_count > 0) {
      header << "  ●";
      if (status.staged_count > 0) {
        header << i18n::tr_fmt("git.header.staged", {std::to_string(status.staged_count)});
      }
      if (status.unstaged_count > 0) {
        if (status.staged_count > 0) {
          header << ", ";
        }
        header << i18n::tr_fmt("git.header.modified", {std::to_string(status.unstaged_count)});
      }
      if (status.untracked_count > 0) {
        if (status.staged_count > 0 || status.unstaged_count > 0) {
          header << ", ";
        }
        header << i18n::tr_fmt("git.header.untracked", {std::to_string(status.untracked_count)});
      }
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

    Element tabs = hbox({
        text(header.str()) | bold | color(theme::Success()),
        filler(),
        MakeTabButton(i18n::tr("git.tab.status"), state->selected_tab == GitPanelState::kTabStatus, hover0, press0,
                      &state->tab_boxes[0]),
        MakeTabButton(i18n::tr("git.tab.log"), state->selected_tab == GitPanelState::kTabLog, hover1, press1,
                      &state->tab_boxes[1]),
        MakeTabButton(i18n::tr("git.tab.branches"), state->selected_tab == GitPanelState::kTabBranches, hover2,
                      press2, &state->tab_boxes[2]),
        MakeTabButton(i18n::tr("git.tab.timeline"), state->selected_tab == GitPanelState::kTabTimeline, hover3,
                      press3, &state->tab_boxes[3]),
        MakeTabButton(i18n::tr("git.tab.graph"), state->selected_tab == GitPanelState::kTabGraph, hover4, press4,
                      &state->tab_boxes[4]),
        state->operation_pending ? text(" " + std::string(spinner::glyph())) | color(theme::Accent())
                               : text(""),
    });

    Elements left_rows;
    Elements center_rows;
    Elements action_rows;

    if (state->selected_tab == GitPanelState::kTabStatus) {
      const auto& entries = status.entries;
      clamp_selection(state, static_cast<int>(entries.size()));
      if (entries.empty()) {
        left_rows.push_back(text(i18n::tr("git.working_tree_clean")) | color(theme::Muted()));
      }
      const int visible = list_visible;
      clamp_selection(state, static_cast<int>(entries.size()));
      scroll_selection_into_view(state, static_cast<int>(entries.size()), visible);
      const int end = std::min(static_cast<int>(entries.size()), state->file_scroll + visible);
      for (int i = state->file_scroll; i < end; ++i) {
        const auto& entry = entries[static_cast<std::size_t>(i)];
        const bool selected = i == state->selected_file;
        Element row = text(" " + status_badge(entry) + " " + entry.path) |
                      color(status_color(entry));
        left_rows.push_back(StyleListRow(std::move(row), selected, false, false));
      }

      if (!entries.empty() && state->selected_file >= 0 &&
          state->selected_file < static_cast<int>(entries.size())) {
        const std::string& path = entries[static_cast<std::size_t>(state->selected_file)].path;
        center_rows.push_back(text(i18n::tr_fmt("git.diff.title", {path})) | color(theme::Accent()) | bold);
        center_rows.push_back(separator() | color(theme::AccentDim()));
        const std::string diff = git->file_diff_text(path);
        const bool loading = diff.empty() && git->busy();
        const int diff_lines = count_diff_lines(diff);
        clamp_diff_scroll(state, diff_lines, diff_visible);
        center_rows.push_back(render_diff_lines(diff, loading, state->diff_scroll, diff_visible));
      } else {
        center_rows.push_back(text(i18n::tr("git.select_file")) | color(theme::Muted()));
      }

      const auto btn = [&](const char* id, const std::string& label, Box* box) {
        const bool hovered = interaction_active(layout_state, id);
        const bool pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(std::string_view(id));
        return MakeToolbarButton(text(label), hovered, pressed, false, box) |
               size(HEIGHT, EQUAL, 1);
      };

      action_rows.push_back(text(i18n::tr("git.actions.title")) | color(theme::Accent()) | bold);
      action_rows.push_back(separator() | color(theme::AccentDim()));
      action_rows.push_back(btn(kGitStage, i18n::tr("git.actions.stage"), &state->stage_box));
      action_rows.push_back(btn(kGitUnstage, i18n::tr("git.actions.unstage"), &state->unstage_box));
      action_rows.push_back(btn(kGitPush, i18n::tr("git.actions.push"), &state->push_box));
      action_rows.push_back(btn(kGitPull, i18n::tr("git.actions.pull"), &state->pull_box));
      action_rows.push_back(text(""));
      action_rows.push_back(text(i18n::tr("git.commit.label")) | color(theme::Accent()) | bold);
      const bool commit_focused = state->commit_input_focus;
      Element commit_line =
          hbox({text(" "), RenderBlinkInputLine(state->commit_message, state->commit_cursor,
                                                commit_focused)}) |
          bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1) | reflect(state->commit_box);
      action_rows.push_back(std::move(commit_line));
      action_rows.push_back(text(i18n::tr("git.commit.confirm_hint")) | color(theme::Muted()));
      action_rows.push_back(text(i18n::tr("git.status.open_diff_hint")) | color(theme::Muted()));
    } else if (state->selected_tab == GitPanelState::kTabLog) {
      const auto commits = visible_log_commits(git, state);
      const bool search_loading =
          !state->log_search_applied.empty() && !git->log_search_ready(state->log_search_applied);
      clamp_selection(state, static_cast<int>(commits.size()));
      const int visible = list_visible;
      scroll_selection_into_view(state, static_cast<int>(commits.size()), visible);
      const int end = std::min(static_cast<int>(commits.size()), state->log_scroll + visible);
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

      const bool search_focused = state->log_search_focus;
      action_rows.push_back(text(i18n::tr("git.log.search.label")) | color(theme::Accent()) | bold);
      action_rows.push_back(separator() | color(theme::AccentDim()));
      Element search_line =
          hbox({text(" "), RenderBlinkInputLine(state->log_search_query, state->log_search_cursor,
                                                search_focused)}) |
          bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1) | reflect(state->log_search_box);
      action_rows.push_back(std::move(search_line));
      action_rows.push_back(text(i18n::tr("git.log.search.hint")) | color(theme::Muted()));

      ensure_commit_files(git, state);
      if (!commits.empty() && state->selected_file >= 0 &&
          state->selected_file < static_cast<int>(commits.size())) {
        const auto& entry = commits[static_cast<std::size_t>(state->selected_file)];
        center_rows.push_back(text(entry.short_hash + " " + entry.message) | color(theme::Accent()) | bold);
        center_rows.push_back(separator() | color(theme::AccentDim()));
        const auto files = git->commit_files(entry.hash);
        const bool files_loading = files.empty() && git->busy() && !git->has_commit_files(entry.hash);
        if (files_loading) {
          center_rows.push_back(text(i18n::tr("git.diff.loading")) | color(theme::Muted()));
        } else if (files.empty()) {
          center_rows.push_back(text(i18n::tr("git.log.no_files")) | color(theme::Muted()));
        } else {
          const auto lines = build_log_commit_view_lines(git, entry.hash, files);
          clamp_diff_scroll(state, static_cast<int>(lines.size()), diff_visible);
          center_rows.push_back(render_log_commit_view(lines, state->diff_scroll, diff_visible));
        }
      } else {
        center_rows.push_back(text(i18n::tr("git.log.title")) | color(theme::Accent()) | bold);
        center_rows.push_back(text(i18n::tr("git.log.navigate_hint")) | color(theme::Muted()));
      }
    } else if (state->selected_tab == GitPanelState::kTabBranches) {
      const auto branches = git->branches();
      clamp_selection(state, static_cast<int>(branches.size()));
      const int visible = list_visible;
      clamp_selection(state, static_cast<int>(branches.size()));
      scroll_selection_into_view(state, static_cast<int>(branches.size()), visible);
      const int end =
          std::min(static_cast<int>(branches.size()), state->branch_scroll + visible);
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
        left_rows.push_back(text(i18n::tr("git.timeline.no_file")) | color(theme::Muted()));
        center_rows.push_back(text(i18n::tr("git.timeline.title")) | color(theme::Accent()) | bold);
        center_rows.push_back(text(i18n::tr("git.timeline.no_file_hint")) | color(theme::Muted()));
      } else {
        const auto commits = git->file_timeline(active_file);
        clamp_selection(state, static_cast<int>(commits.size()));
        const int visible = list_visible;
        scroll_selection_into_view(state, static_cast<int>(commits.size()), visible);
        const int end = std::min(static_cast<int>(commits.size()), state->timeline_scroll + visible);
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

        action_rows.push_back(text(i18n::tr("git.timeline.file")) | color(theme::Accent()) | bold);
        action_rows.push_back(separator() | color(theme::AccentDim()));
        action_rows.push_back(paragraphAlignLeft(" " + active_file) | color(theme::Header()));
        action_rows.push_back(text(""));
        action_rows.push_back(text(i18n::tr("git.timeline.navigate_hint")) | color(theme::Muted()));

        if (!commits.empty() && state->selected_file >= 0 &&
            state->selected_file < static_cast<int>(commits.size())) {
          const auto& entry = commits[static_cast<std::size_t>(state->selected_file)];
          center_rows.push_back(text(entry.short_hash + " " + entry.message) | color(theme::Accent()) |
                                bold);
          center_rows.push_back(text(entry.author + " · " + entry.date) | color(theme::Muted()));
          center_rows.push_back(separator() | color(theme::AccentDim()));
          const std::string diff = git->timeline_diff_text(active_file, entry.hash);
          const bool loading = diff.empty() && git->busy();
          const int diff_lines = count_diff_lines(diff);
          clamp_diff_scroll(state, diff_lines, diff_visible);
          center_rows.push_back(render_diff_lines(diff, loading, state->diff_scroll, diff_visible));
        } else if (!commits.empty()) {
          center_rows.push_back(text(i18n::tr("git.timeline.select_commit")) | color(theme::Muted()));
        } else {
          center_rows.push_back(text(i18n::tr("git.timeline.title")) | color(theme::Accent()) | bold);
        }
      }
    } else {
      git->refresh_graph();
      left_rows.push_back(text(i18n::tr("git.graph.title")) | color(theme::Accent()) | bold);
      left_rows.push_back(text(i18n::tr("git.graph.scroll_hint")) | color(theme::Muted()));
      const auto lines = git->graph_lines();
      center_rows.push_back(text(i18n::tr("git.graph.title")) | color(theme::Accent()) | bold);
      center_rows.push_back(separator() | color(theme::AccentDim()));
      if (lines.empty()) {
        const bool loading = !git->graph_loaded() && git->busy();
        center_rows.push_back(text(loading ? i18n::tr("git.diff.loading") : i18n::tr("git.graph.empty")) |
                            color(theme::Muted()));
      } else {
        const int end = std::min(static_cast<int>(lines.size()), state->graph_scroll + diff_visible);
        state->graph_scroll =
            std::max(0, std::min(state->graph_scroll, std::max(0, static_cast<int>(lines.size()) - diff_visible)));
        for (int i = state->graph_scroll; i < end; ++i) {
          center_rows.push_back(render_graph_line(lines[static_cast<std::size_t>(i)]));
        }
      }
      action_rows.push_back(text(i18n::tr("git.graph.legend")) | color(theme::Accent()) | bold);
      action_rows.push_back(separator() | color(theme::AccentDim()));
      action_rows.push_back(text(i18n::tr("git.graph.legend_branches")) | color(theme::Success()));
      action_rows.push_back(text(i18n::tr("git.graph.legend_graph")) | color(theme::Accent()));
    }

    if (!state->status_message.empty()) {
      action_rows.push_back(text(""));
      action_rows.push_back(paragraphAlignLeft(" " + state->status_message) |
                          color(theme::Warning()));
    }

    Element body = hbox({
        vbox(std::move(left_rows)) | reflect(state->file_list_box) |
            size(WIDTH, EQUAL, kFileListWidth) | bgcolor(theme::CodeBg()) | border,
        separator(),
        vbox(std::move(center_rows)) | reflect(state->diff_box) | flex | bgcolor(theme::CodeBg()) |
            border,
        separator(),
        vbox(std::move(action_rows)) | size(WIDTH, EQUAL, kActionWidth) | bgcolor(theme::PanelBg()) |
            border,
    });

    Element result = vbox({
               tabs | size(HEIGHT, EQUAL, 1),
               separator(),
               std::move(body) | flex,
           }) |
           flex | bgcolor(theme::PanelBg()) | reflect(state->panel_box);
    return result;
  });

  return WrapFocusable(CatchEvent(panel, [dispatch_keys, dispatch_mouse](Event event) {
    if (dispatch_mouse(event)) {
      return true;
    }
    return dispatch_keys(event);
  }));
}

}  // namespace tuide
