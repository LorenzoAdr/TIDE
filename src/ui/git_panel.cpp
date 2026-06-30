#include "ui/git_panel.hpp"

#include <algorithm>
#include <sstream>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/clickable.hpp"
#include "ui/cursor_blink.hpp"
#include "ui/focusable_component.hpp"
#include "ui/main_layout.hpp"
#include "ui/panel.hpp"
#include "ui/spinner.hpp"
#include "ui/text_input_style.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kFileListWidth = 28;
constexpr int kActionWidth = 26;

constexpr const char* kGitTab0 = "git-tab-0";
constexpr const char* kGitTab1 = "git-tab-1";
constexpr const char* kGitTab2 = "git-tab-2";
constexpr const char* kGitStage = "git-stage";
constexpr const char* kGitUnstage = "git-unstage";
constexpr const char* kGitPush = "git-push";
constexpr const char* kGitPull = "git-pull";
constexpr const char* kGitCommit = "git-commit";

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
    return text(" cargando diff…") | color(theme::Muted());
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
    rows.push_back(text(" (sin cambios)") | color(theme::Muted()));
  }
  return vbox(std::move(rows)) | flex;
}

void set_status(GitPanelState* state, const std::string& message) {
  if (state != nullptr) {
    state->status_message = message;
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
  if (!git->has_file_diff_text(path)) {
    git->refresh_file_diff(path);
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
  if (git == nullptr) {
    return;
  }
  if (tab == GitPanelState::kTabLog) {
    git->refresh_log();
  } else if (tab == GitPanelState::kTabBranches) {
    git->refresh_branches();
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
    set_status(state, ok ? "staged" : msg);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
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
    set_status(state, ok ? "unstaged" : msg);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
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
    set_status(state, ok ? "commit ok" : msg);
    if (layout_state != nullptr) {
      layout_state->request_ui_tick = true;
    }
  });
}

bool handle_git_keys(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                     FocusManagerState* focus, Event event) {
  if (state == nullptr || git == nullptr || !git->is_repo()) {
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

  if (state->selected_tab == GitPanelState::kTabStatus) {
    const int count = static_cast<int>(git->status().entries.size());
    if (event == Event::ArrowUp) {
      if (state->selected_file > 0) {
        --state->selected_file;
      }
      ensure_selected_diff(git, state);
      return true;
    }
    if (event == Event::ArrowDown) {
      if (count > 0) {
        state->selected_file = std::min(state->selected_file + 1, count - 1);
      }
      ensure_selected_diff(git, state);
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
        set_status(state, ok ? "push ok" : msg);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      });
      return true;
    }
    if (event == Event::Character('P')) {
      state->operation_pending = true;
      git->pull([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? "pull ok" : msg);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      });
      return true;
    }
    if (event == Event::Character('c')) {
      state->commit_input_focus = true;
      return true;
    }
    if (state->commit_input_focus) {
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
  } else if (state->selected_tab == GitPanelState::kTabLog) {
    const int count = static_cast<int>(git->log_entries().size());
    if (event == Event::ArrowUp && state->selected_file > 0) {
      --state->selected_file;
      return true;
    }
    if (event == Event::ArrowDown && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      return true;
    }
  } else if (state->selected_tab == GitPanelState::kTabBranches) {
    const int count = static_cast<int>(git->branches().size());
    if (event == Event::ArrowUp && state->selected_file > 0) {
      --state->selected_file;
      return true;
    }
    if (event == Event::ArrowDown && count > 0) {
      state->selected_file = std::min(state->selected_file + 1, count - 1);
      return true;
    }
    if (event == Event::Return) {
      if (state->selected_file >= 0 && state->selected_file < count) {
        const std::string name =
            git->branches()[static_cast<std::size_t>(state->selected_file)].name;
        state->operation_pending = true;
        git->checkout_branch(name, [state, layout_state](bool ok, const std::string& msg) {
          state->operation_pending = false;
          set_status(state, ok ? "rama cambiada" : msg);
          if (layout_state != nullptr) {
            layout_state->request_ui_tick = true;
          }
        });
      }
      return true;
    }
  }

  if (event == Event::Escape || event == Event::CtrlE) {
    if (layout_state != nullptr) {
      layout_state->git_page_visible = false;
      layout_state->focus_sync_needed = true;
    }
    if (focus != nullptr) {
      focus->region = FocusRegion::Editor;
    }
    return true;
  }

  return false;
}

bool handle_git_mouse(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                      FocusManagerState* focus, Event event) {
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
         {kGitStage, &state->stage_box},
         {kGitUnstage, &state->unstage_box},
         {kGitPush, &state->push_box},
         {kGitPull, &state->pull_box},
         {kGitCommit, &state->commit_box}},
        [](std::string_view id) {
          return id.rfind("git-", 0) == 0;
        });
    return false;
  }

  if (!state->panel_box.Contain(m.x, m.y)) {
    return false;
  }

  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
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

  if (state->file_list_box.Contain(m.x, m.y)) {
    const int local_row = std::max(0, m.y - state->file_list_box.y_min);
    int index = 0;
    int count = 0;
    if (state->selected_tab == GitPanelState::kTabStatus) {
      index = state->file_scroll + local_row;
      count = static_cast<int>(git->status().entries.size());
    } else if (state->selected_tab == GitPanelState::kTabLog) {
      index = state->log_scroll + local_row;
      count = static_cast<int>(git->log_entries().size());
    } else {
      index = state->branch_scroll + local_row;
      count = static_cast<int>(git->branches().size());
    }
    if (index >= 0 && index < count) {
      state->selected_file = index;
      ensure_selected_diff(git, state);
    }
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
        set_status(state, ok ? "push ok" : msg);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      });
      return true;
    }
    if (state->pull_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitPull));
      state->operation_pending = true;
      git->pull([state, layout_state](bool ok, const std::string& msg) {
        state->operation_pending = false;
        set_status(state, ok ? "pull ok" : msg);
        if (layout_state != nullptr) {
          layout_state->request_ui_tick = true;
        }
      });
      return true;
    }
    if (state->commit_box.Contain(m.x, m.y)) {
      trigger_press(layout_state, std::string_view(kGitCommit));
      state->commit_input_focus = true;
      return true;
    }
  }

  return false;
}

}  // namespace

void GitPanelEnsureSelectedDiff(GitService* git, GitPanelState* state) {
  ensure_selected_diff(git, state);
}

Component MakeGitPanel(GitService* git, GitPanelState* state, MainLayoutState* layout_state,
                       FocusManagerState* focus) {
  auto dispatch_keys = [git, state, layout_state, focus](Event event) {
    return handle_git_keys(git, state, layout_state, focus, event);
  };
  auto dispatch_mouse = [git, state, layout_state, focus](Event event) {
    return handle_git_mouse(git, state, layout_state, focus, event);
  };

  if (layout_state != nullptr) {
    layout_state->git_key_handler = dispatch_keys;
    layout_state->git_mouse_handler = dispatch_mouse;
  }

  auto panel = Renderer([git, state, layout_state] {
    if (git == nullptr || state == nullptr) {
      return text(" Git no disponible") | color(theme::Muted()) | flex;
    }

    const GitRepoInfo repo = git->repo_info();
    if (!repo.valid) {
      return vbox({
                 text(" Git") | bold | color(theme::Success()),
                 separator(),
                 text(" " + (repo.last_error.empty() ? "No es un repositorio git"
                                                      : repo.last_error)) |
                     color(theme::Muted()),
             }) |
             flex | bgcolor(theme::PanelBg()) | reflect(state->panel_box);
    }

    std::ostringstream header;
    header << " Git  " << repo.branch;
    const GitStatusSnapshot status = git->status();
    if (status.staged_count > 0 || status.unstaged_count > 0 || status.untracked_count > 0) {
      header << "  ●";
      if (status.staged_count > 0) {
        header << status.staged_count << " staged";
      }
      if (status.unstaged_count > 0) {
        if (status.staged_count > 0) {
          header << ", ";
        }
        header << status.unstaged_count << " modified";
      }
      if (status.untracked_count > 0) {
        if (status.staged_count > 0 || status.unstaged_count > 0) {
          header << ", ";
        }
        header << status.untracked_count << " untracked";
      }
    }

    const bool hover0 = interaction_active(layout_state, kGitTab0);
    const bool hover1 = interaction_active(layout_state, kGitTab1);
    const bool hover2 = interaction_active(layout_state, kGitTab2);
  const bool press0 =
      layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab0));
  const bool press1 =
      layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab1));
  const bool press2 =
      layout_state != nullptr && layout_state->clickable.is_pressed(std::string_view(kGitTab2));

    Element tabs = hbox({
        MakeTabButton("1 Status", state->selected_tab == GitPanelState::kTabStatus, hover0, press0,
                      &state->tab_boxes[0]),
        MakeTabButton("2 Log", state->selected_tab == GitPanelState::kTabLog, hover1, press1,
                      &state->tab_boxes[1]),
        MakeTabButton("3 Branches", state->selected_tab == GitPanelState::kTabBranches, hover2,
                      press2, &state->tab_boxes[2]),
        filler(),
        state->operation_pending ? text(spinner::glyph()) | color(theme::Accent()) : text(""),
        text(" F5/Esc salir") | color(theme::Muted()),
    });

    Elements left_rows;
    Elements center_rows;
    Elements action_rows;

    if (state->selected_tab == GitPanelState::kTabStatus) {
      const auto& entries = status.entries;
      clamp_selection(state, static_cast<int>(entries.size()));
      if (entries.empty()) {
        left_rows.push_back(text(" (working tree limpio)") | color(theme::Muted()));
      }
      const int visible = 20;
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
        center_rows.push_back(text(" Diff: " + path) | color(theme::Accent()) | bold);
        center_rows.push_back(separator() | color(theme::AccentDim()));
        const std::string diff = git->file_diff_text(path);
        const bool loading = diff.empty() && git->busy();
        center_rows.push_back(render_diff_lines(diff, loading, 0, 18));
      } else {
        center_rows.push_back(text(" Selecciona un archivo") | color(theme::Muted()));
      }

      const auto btn = [&](const char* id, const char* label, Box* box) {
        const bool hovered = interaction_active(layout_state, id);
        const bool pressed =
            layout_state != nullptr &&
            layout_state->clickable.is_pressed(std::string_view(id));
        return MakeToolbarButton(text(label), hovered, pressed, false, box) |
               size(HEIGHT, EQUAL, 1);
      };

      action_rows.push_back(text(" Acciones") | color(theme::Accent()) | bold);
      action_rows.push_back(separator() | color(theme::AccentDim()));
      action_rows.push_back(btn(kGitStage, " Stage", &state->stage_box));
      action_rows.push_back(btn(kGitUnstage, " Unstage", &state->unstage_box));
      action_rows.push_back(btn(kGitPush, " Push", &state->push_box));
      action_rows.push_back(btn(kGitPull, " Pull", &state->pull_box));
      action_rows.push_back(text(""));
      action_rows.push_back(text(" Commit:") | color(theme::Accent()) | bold);
      const bool commit_focused = state->commit_input_focus;
      Element commit_line =
          hbox({text(" "), RenderBlinkInputLine(state->commit_message, state->commit_cursor,
                                                commit_focused)}) |
          bgcolor(theme::TabIdle()) | size(HEIGHT, EQUAL, 1) | reflect(state->commit_box);
      action_rows.push_back(std::move(commit_line));
      action_rows.push_back(text(" Enter o clic confirmar") | color(theme::Muted()));
    } else if (state->selected_tab == GitPanelState::kTabLog) {
      const auto commits = git->log_entries();
      clamp_selection(state, static_cast<int>(commits.size()));
      const int visible = 22;
      const int end = std::min(static_cast<int>(commits.size()), state->log_scroll + visible);
      for (int i = state->log_scroll; i < end; ++i) {
        const auto& entry = commits[static_cast<std::size_t>(i)];
        const bool selected = i == state->selected_file;
        Element row = text(" " + entry.short_hash + " " + entry.message) | color(theme::Header());
        left_rows.push_back(StyleListRow(std::move(row), selected, false, false));
      }
      if (commits.empty()) {
        left_rows.push_back(text(" (sin commits)") | color(theme::Muted()));
      }
      center_rows.push_back(text(" Historial de commits") | color(theme::Accent()) | bold);
      center_rows.push_back(text(" Clic o ↑/↓ para navegar") | color(theme::Muted()));
    } else {
      const auto branches = git->branches();
      clamp_selection(state, static_cast<int>(branches.size()));
      const int visible = 22;
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
        left_rows.push_back(text(" (sin ramas)") | color(theme::Muted()));
      }
      center_rows.push_back(text(" Ramas") | color(theme::Accent()) | bold);
      center_rows.push_back(text(" Clic o Enter para cambiar rama") | color(theme::Muted()));
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
        vbox(std::move(center_rows)) | flex | bgcolor(theme::CodeBg()) | border,
        separator(),
        vbox(std::move(action_rows)) | size(WIDTH, EQUAL, kActionWidth) | bgcolor(theme::PanelBg()) |
            border,
    });

    Element result = vbox({
               text(header.str()) | bold | color(theme::Success()),
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

}  // namespace tgdb
