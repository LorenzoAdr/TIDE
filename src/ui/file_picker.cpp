#include "ui/file_picker.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/focus_manager.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool contains_insensitive(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

std::string picker_display_path(const std::string& path, const std::string& workspace_root) {
  if (path.empty()) {
    return path;
  }
  std::error_code ec;
  const std::filesystem::path absolute = std::filesystem::path(path).is_absolute()
                                             ? std::filesystem::path(path)
                                             : std::filesystem::path(workspace_root) / path;
  if (!workspace_root.empty()) {
    const auto relative =
        std::filesystem::relative(absolute, std::filesystem::path(workspace_root), ec);
    if (!ec && !relative.empty()) {
      return relative.string();
    }
  }
  return absolute.filename().string();
}

std::filesystem::path picker_absolute_path(const std::string& match,
                                           const std::string& workspace_root) {
  std::error_code ec;
  const std::filesystem::path match_path(match);
  if (match_path.is_absolute()) {
    return std::filesystem::absolute(match_path, ec);
  }
  return std::filesystem::absolute(std::filesystem::path(workspace_root) / match, ec);
}

}  // namespace

void FilePickerState::sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                                 const std::string& workspace_root) {
  if (!snapshot || snapshot->workspace_root != workspace_root) {
    return;
  }
  if (indexed_root == workspace_root && all_files == snapshot->files) {
    return;
  }
  indexed_root = workspace_root;
  all_files = snapshot->files;
}

void FilePickerState::refresh_matches(const WorkspaceModel* workspace) {
  matches.clear();
  if (query.empty() && workspace != nullptr) {
    matches = workspace->open_tabs_mru_excluding_active();
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
    return;
  }
  for (const auto& path : all_files) {
    const std::string filename = std::filesystem::path(path).filename().string();
    if (contains_insensitive(filename, query)) {
      matches.push_back(path);
    }
  }
  if (selected >= static_cast<int>(matches.size())) {
    selected = std::max(0, static_cast<int>(matches.size()) - 1);
  }
}

void FilePickerState::open_file(DebugModel* model, WorkspaceModel* workspace,
                                FocusManagerState* focus, int index) {
  if (matches.empty()) {
    return;
  }
  index = std::max(0, std::min(index, static_cast<int>(matches.size()) - 1));
  std::error_code ec;
  const auto absolute =
      picker_absolute_path(matches[static_cast<std::size_t>(index)], model->workspace_root);
  if (workspace != nullptr) {
    if (!workspace->open_file(absolute.string())) {
      return;
    }
  }
  model->active_file = absolute.string();
  model->active_line = 0;
  model->view_token++;
  if (focus != nullptr) {
    focus->region = FocusRegion::Editor;
  }
  open = false;
  query.clear();
  selected = 0;
  refresh_matches(workspace);
}

Component MakeFilePickerOverlay(Component main, DebugModel* model,
                                WorkspaceModel* workspace, FilePickerState* state,
                                FocusManagerState* focus, WorkspaceIndexer* indexer) {
  return Renderer(
      CatchEvent(main, [model, workspace, state, focus, indexer](Event event) {
        if (!state->open) {
          return false;
        }

        state->sync_index(indexer != nullptr ? indexer->snapshot() : nullptr,
                          model->workspace_root);

        if (event == Event::Escape) {
          state->open = false;
          state->query.clear();
          state->selected = 0;
          state->refresh_matches(workspace);
          return true;
        }
        if (event == Event::Return) {
          state->open_file(model, workspace, focus, state->selected);
          return true;
        }
        if (event == Event::ArrowDown) {
          if (!state->matches.empty()) {
            state->selected = std::min(state->selected + 1,
                                       static_cast<int>(state->matches.size()) - 1);
          }
          return true;
        }
        if (event == Event::ArrowUp) {
          state->selected = std::max(0, state->selected - 1);
          return true;
        }
        if (event == Event::CtrlP) {
          if (!state->matches.empty()) {
            state->selected =
                (state->selected + 1) % static_cast<int>(state->matches.size());
          }
          return true;
        }
        if (event == Event::Backspace) {
          if (!state->query.empty()) {
            state->query.pop_back();
            state->selected = 0;
            state->refresh_matches(workspace);
          }
          return true;
        }
        if (event.is_character()) {
          const std::string ch = event.character();
          if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
              static_cast<unsigned char>(ch[0]) < 127) {
            state->query += ch;
            state->selected = 0;
            state->refresh_matches(workspace);
          }
          return true;
        }
        return true;
      }),
      [main, model, workspace, state, indexer] {
        Element base = main->Render();
        if (!state->open) {
          return base;
        }

        state->sync_index(indexer != nullptr ? indexer->snapshot() : nullptr,
                          model->workspace_root);
        state->refresh_matches(workspace);

        std::string input_line = state->query;
        input_line.push_back('_');

        Elements matches;
        const int max_rows = 14;
        const int start = std::max(
            0, std::min(state->selected,
                        std::max(0, static_cast<int>(state->matches.size()) - max_rows)));
        const int end =
            std::min(static_cast<int>(state->matches.size()), start + max_rows);
        for (int i = start; i < end; ++i) {
          const std::string label =
              picker_display_path(state->matches[static_cast<std::size_t>(i)],
                                  model->workspace_root);
          Element row = text(label) | color(theme::Header());
          if (i == state->selected) {
            row = row | inverted | bold;
          }
          matches.push_back(row);
        }
        if (matches.empty()) {
          const bool scanning = indexer != nullptr && indexer->scanning();
          const char* empty_label = state->query.empty()
                                        ? "(sin archivos abiertos)"
                                        : (scanning ? "(indexando...)" : "(sin coincidencias)");
          matches.push_back(text(empty_label) | color(theme::Muted()));
        }

        const char* title = state->query.empty() ? "Archivos abiertos" : "Buscar archivo";
        Element dialog = ModalWindow(
            text(title) | color(theme::Accent()),
            vbox({ModalInputLine(input_line),
                  separator(),
                  vbox(std::move(matches)) | frame | vscroll_indicator |
                      bgcolor(theme::PanelBg())}));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
