#include "ui/file_picker.hpp"

#include "ui/key_bindings.hpp"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_set>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/focus_manager.hpp"
#include "ui/panel.hpp"
#include "ui/key_bindings.hpp"
#include "ui/theme.hpp"
#include "i18n/tr.hpp"
#include "util/cpp_highlight.hpp"
#include "util/path_normalize.hpp"
#include "util/build_file_highlight.hpp"
#include "util/fuzzy_match.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kModalWidth = 120;
constexpr int kLeftPaneWidth = 54;
constexpr int kRightPaneWidth = kModalWidth - kLeftPaneWidth - 1;
constexpr int kPaneHeight = 18;
constexpr int kMaxMatchRows = kPaneHeight;
constexpr int kMaxPreviewRows = kPaneHeight;
constexpr int kMatchTextWidth = kLeftPaneWidth - 2;
constexpr int kOpenTabScoreBonus = 1000;
constexpr int kSameDirectoryBonus = 400;
constexpr int kSharedPathComponentBonus = 80;

std::string picker_directory_label(std::string_view label) {
  const std::size_t slash = label.find_last_of("/\\");
  if (slash == std::string::npos) {
    return {};
  }
  return std::string(label.substr(0, slash));
}

int count_shared_path_components(std::string_view a, std::string_view b) {
  int shared = 0;
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size() && j < b.size()) {
    while (i < a.size() && (a[i] == '/' || a[i] == '\\')) {
      ++i;
    }
    while (j < b.size() && (b[j] == '/' || b[j] == '\\')) {
      ++j;
    }
    if (i >= a.size() || j >= b.size()) {
      break;
    }

    const std::size_t end_a = a.find_first_of("/\\", i);
    const std::size_t end_b = b.find_first_of("/\\", j);
    const std::size_t seg_end_a = end_a == std::string::npos ? a.size() : end_a;
    const std::size_t seg_end_b = end_b == std::string::npos ? b.size() : end_b;
    if (a.substr(i, seg_end_a - i) != b.substr(j, seg_end_b - j)) {
      break;
    }
    ++shared;
    i = seg_end_a;
    j = seg_end_b;
  }
  return shared;
}

int path_proximity_bonus(std::string_view ref_dir, std::string_view candidate_label) {
  if (ref_dir.empty()) {
    return 0;
  }
  const std::string candidate_dir = picker_directory_label(candidate_label);
  if (candidate_dir.empty()) {
    return 0;
  }
  if (ref_dir == candidate_dir) {
    return kSameDirectoryBonus;
  }
  return count_shared_path_components(ref_dir, candidate_dir) * kSharedPathComponentBonus;
}

Element render_fuzzy_chars(const std::string& segment, std::size_t label_offset,
                           const std::unordered_set<std::size_t>& hits, Color base_color) {
  Elements parts;
  std::string run;
  Color run_color = base_color;
  auto flush = [&]() {
    if (!run.empty()) {
      parts.push_back(text(run) | color(run_color));
      run.clear();
    }
  };
  for (std::size_t i = 0; i < segment.size(); ++i) {
    const std::size_t label_index = label_offset + i;
    const Color want = hits.count(label_index) != 0 ? theme::Error() : base_color;
    if (!run.empty() && want != run_color) {
      flush();
    }
    run_color = want;
    run.push_back(segment[i]);
  }
  flush();
  return parts.size() == 1 ? parts[0] : hbox(std::move(parts));
}

Element render_fuzzy_path_label(const std::string& label,
                                const std::vector<std::size_t>& indices, bool selected,
                                int max_width) {
  std::unordered_set<std::size_t> hits(indices.begin(), indices.end());

  const std::size_t slash = label.find_last_of("/\\");
  const bool has_dir = slash != std::string::npos;
  const std::string filename = has_dir ? label.substr(slash + 1) : label;
  const std::size_t filename_offset = has_dir ? slash + 1 : 0;

  if (static_cast<int>(filename.size()) > max_width) {
    const std::size_t visible_offset =
        filename.size() - static_cast<std::size_t>(max_width);
    Element row = render_fuzzy_chars(filename.substr(visible_offset),
                                     filename_offset + visible_offset, hits, theme::Header());
    if (selected) {
      row = row | inverted | bold;
    }
    return row;
  }

  const std::string dirname = has_dir ? label.substr(0, slash + 1) : std::string{};
  int dir_budget = max_width - static_cast<int>(filename.size());
  if (dir_budget < 0) {
    dir_budget = 0;
  }

  Elements row_parts;
  if (has_dir && dir_budget > 0) {
    std::string dir_visible = dirname;
    std::size_t dir_label_offset = 0;
    if (static_cast<int>(dirname.size()) > dir_budget) {
      constexpr const char* kEllipsis = "…";
      const int tail_budget = dir_budget - 1;
      if (tail_budget <= 0) {
        dir_visible = kEllipsis;
        dir_label_offset = dirname.size();
      } else {
        std::string tail = dirname.substr(dirname.size() - static_cast<std::size_t>(tail_budget));
        const auto sep = tail.find_first_of("/\\");
        if (sep != std::string::npos) {
          tail = tail.substr(sep + 1);
        }
        dir_visible = std::string(kEllipsis) + tail;
        dir_label_offset = dirname.size() - tail.size();
      }
    }
    row_parts.push_back(render_fuzzy_chars(dir_visible, dir_label_offset, hits, theme::Muted()));
  }

  row_parts.push_back(
      render_fuzzy_chars(filename, filename_offset, hits, theme::Header()));

  Element row = row_parts.size() == 1 ? row_parts[0] : hbox(std::move(row_parts));
  if (selected) {
    row = row | inverted | bold;
  }
  return row;
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

std::string selected_absolute_path(const FilePickerState* state, const std::string& workspace_root) {
  if (state->matches.empty()) {
    return {};
  }
  const int index =
      std::max(0, std::min(state->selected, static_cast<int>(state->matches.size()) - 1));
  return picker_absolute_path(state->matches[static_cast<std::size_t>(index)].path, workspace_root)
      .string();
}

Element render_preview_line(const FilePickerPreviewData& preview, const std::string& line,
                            CppHighlightContext* ctx) {
  if (preview.build_file_kind != BuildFileKind::kNone) {
    return HighlightBuildFileLine(preview.build_file_kind, line);
  }
  if (preview.use_cpp_highlight) {
    return HighlightCppLine(line, -1, {}, 0, ctx);
  }
  return text(line.empty() ? " " : line) | color(theme::SyntaxDefault());
}

Element render_preview_panel(const FilePickerPreviewData& preview, const std::string& workspace_root) {
  const std::string header =
      preview.path.empty() ? " " : picker_display_path(preview.path, workspace_root);

  Elements body;
  switch (preview.state) {
    case FilePickerPreviewState::kIdle:
      body.push_back(text(" ") | color(theme::Muted()));
      break;
    case FilePickerPreviewState::kLoading:
      body.push_back(text(i18n::tr("picker.file.preview.loading")) | color(theme::Muted()));
      break;
    case FilePickerPreviewState::kUnsupported: {
      const char* key = "picker.file.preview.unsupported";
      switch (preview.unsupported_reason) {
        case FilePickerPreviewUnsupportedReason::kBinary:
          key = "picker.file.preview.binary";
          break;
        case FilePickerPreviewUnsupportedReason::kTabular:
          key = "picker.file.preview.tabular";
          break;
        case FilePickerPreviewUnsupportedReason::kPdf:
          key = "picker.file.preview.pdf";
          break;
        default:
          break;
      }
      body.push_back(text(i18n::tr(key)) | color(theme::Muted()));
      break;
    }
    case FilePickerPreviewState::kError:
      body.push_back(text(i18n::tr("picker.file.preview.error")) | color(theme::Muted()));
      break;
    case FilePickerPreviewState::kReady: {
      CppHighlightContext highlight_ctx;
      const int line_count = static_cast<int>(preview.lines.size());
      const int end = std::min(line_count, kMaxPreviewRows);
      for (int i = 0; i < end; ++i) {
        const std::string& line = preview.lines[static_cast<std::size_t>(i)];
        const std::string line_no = std::to_string(i + 1);
        const int gutter = std::max(4, static_cast<int>(line_no.size()) + 1);
        Element row = hbox({
            text(line_no + std::string(static_cast<std::size_t>(gutter - line_no.size()), ' ')) |
                color(theme::Muted()) | size(WIDTH, EQUAL, gutter),
            render_preview_line(preview, line, &highlight_ctx),
        });
        body.push_back(std::move(row));
      }
      if (line_count > kMaxPreviewRows) {
        body.push_back(text(i18n::tr("common.ellipsis")) | color(theme::Muted()));
      }
      break;
    }
  }

  return vbox({
             hbox({text(" " + header) | color(theme::Accent()) | bold, filler()}) |
                 size(HEIGHT, EQUAL, 1),
             separator(),
             vbox(std::move(body)) | frame | vscroll_indicator | bgcolor(theme::CodeBg()),
         }) |
         size(WIDTH, EQUAL, kRightPaneWidth) | size(HEIGHT, EQUAL, kPaneHeight + 2);
}

}  // namespace

void FilePickerState::sync_index(const std::shared_ptr<const IndexSnapshot>& snapshot,
                                 const std::string& workspace_root) {
  if (!snapshot || snapshot->workspace_root != workspace_root) {
    return;
  }
  if (indexed_root == workspace_root && index_snapshot.get() == snapshot.get()) {
    return;
  }
  indexed_root = workspace_root;
  index_snapshot = snapshot;
  all_files = snapshot->files;
  all_files_lower = snapshot->files_lower;
  matches_dirty = true;
}

void FilePickerState::refresh_matches(const WorkspaceModel* workspace) {
  if (!matches_dirty) {
    return;
  }
  matches_dirty = false;
  matches.clear();
  if (query.empty() && workspace != nullptr) {
    for (const std::string& path : workspace->open_tabs_mru_excluding_active()) {
      matches.push_back({path, 0, {}});
    }
    if (selected >= static_cast<int>(matches.size())) {
      selected = std::max(0, static_cast<int>(matches.size()) - 1);
    }
    return;
  }

  const std::string& workspace_root =
      !indexed_root.empty() ? indexed_root
                            : (workspace != nullptr ? workspace->root : std::string{});

  struct Candidate {
    std::string path;
    std::string label;
    int score = 0;
    std::vector<std::size_t> match_indices;
  };
  std::vector<Candidate> candidates;
  std::unordered_set<std::string> seen;
  const std::string query_lower = fuzzy_to_lower(query);

  std::string ref_dir;
  if (workspace != nullptr && !workspace->active_file.empty()) {
    ref_dir = picker_directory_label(picker_display_path(workspace->active_file, workspace_root));
  }

  const auto try_add = [&](const std::string& path, std::string_view label,
                           std::string_view label_lower, int bonus) {
    const FuzzyMatchResult result = fuzzy_match_cached(label, label_lower, query_lower);
    if (!result.matched) {
      return;
    }
    const auto absolute = picker_absolute_path(path, workspace_root);
    const std::string normalized = normalize_path(absolute.string());
    if (seen.count(normalized) != 0) {
      return;
    }
    seen.insert(normalized);
    const int proximity = path_proximity_bonus(ref_dir, label);
    candidates.push_back(
        {path, std::string(label), result.score + bonus + proximity, result.indices});
  };

  if (workspace != nullptr) {
    for (const std::string& path : workspace->open_tabs_mru()) {
      const std::string label = picker_display_path(path, workspace_root);
      try_add(path, label, fuzzy_to_lower(label), kOpenTabScoreBonus);
    }
  }

  for (std::size_t i = 0; i < all_files.size(); ++i) {
    const std::string& path = all_files[i];
    const std::string& label_lower =
        i < all_files_lower.size() ? all_files_lower[i] : fuzzy_to_lower(path);
    try_add(path, path, label_lower, 0);
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate& a, const Candidate& b) {
              if (a.score != b.score) {
                return a.score > b.score;
              }
              if (a.label.size() != b.label.size()) {
                return a.label.size() < b.label.size();
              }
              return a.label < b.label;
            });

  matches.reserve(candidates.size());
  for (const Candidate& candidate : candidates) {
    matches.push_back({candidate.path, candidate.score, candidate.match_indices});
  }
  if (selected >= static_cast<int>(matches.size())) {
    selected = std::max(0, static_cast<int>(matches.size()) - 1);
  }
}

void FilePickerState::mark_matches_dirty() {
  matches_dirty = true;
}

void FilePickerState::set_preview_notify(std::function<void()> notify) {
  preview.set_notify_callback(std::move(notify));
}

void FilePickerState::set_repaint_notify(std::function<void()> notify) {
  repaint_notify = std::move(notify);
}

void FilePickerState::update_preview_for_selection(const std::string& workspace_root) {
  if (!open) {
    return;
  }
  const std::string absolute = selected_absolute_path(this, workspace_root);
  if (absolute == preview_requested_path) {
    return;
  }
  preview_requested_path = absolute;
  preview.request(absolute);
}

void FilePickerState::reset_preview() {
  preview_requested_path.clear();
  preview.reset();
}

void FilePickerState::on_opened(const std::string& workspace_root) {
  update_preview_for_selection(workspace_root);
}

void FilePickerState::on_closed() {
  cancel_ctrl_chord();
  reset_preview();
}

void FilePickerState::arm_ctrl_chord() {
  ctrl_chord_armed = true;
  ctrl_chord_active = false;
}

void FilePickerState::cancel_ctrl_chord() {
  ctrl_chord_armed = false;
  ctrl_chord_active = false;
}

void FilePickerState::confirm_ctrl_chord_selection(DebugModel* model, WorkspaceModel* workspace,
                                                 FocusManagerState* focus) {
  if (!ctrl_chord_active || matches.empty()) {
    cancel_ctrl_chord();
    return;
  }
  open_file(model, workspace, focus, selected);
}

void FilePickerState::open_file(DebugModel* model, WorkspaceModel* workspace,
                                FocusManagerState* focus, int index) {
  if (matches.empty()) {
    return;
  }
  index = std::max(0, std::min(index, static_cast<int>(matches.size()) - 1));
  std::error_code ec;
  const auto absolute =
      picker_absolute_path(matches[static_cast<std::size_t>(index)].path, model->workspace_root);
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
  mark_matches_dirty();
  refresh_matches(workspace);
  on_closed();
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

        if (event == Event::Custom) {
          return false;
        }
        if (event_is_kitty_key_release(event)) {
          return false;
        }

        if (event == Event::Escape) {
          state->open = false;
          state->query.clear();
          state->selected = 0;
          state->mark_matches_dirty();
          state->refresh_matches(workspace);
          state->on_closed();
          return true;
        }
        if (event_is_ctrl_key_release(event)) {
          if (state->ctrl_chord_active) {
            state->confirm_ctrl_chord_selection(model, workspace, focus);
          } else {
            state->ctrl_chord_armed = false;
          }
          if (state->repaint_notify) {
            state->repaint_notify();
          }
          return true;
        }
        if (event == Event::Return) {
          state->open_file(model, workspace, focus, state->selected);
          return true;
        }
        if (event == Event::ArrowDown) {
          state->cancel_ctrl_chord();
          if (!state->matches.empty()) {
            state->selected = std::min(state->selected + 1,
                                       static_cast<int>(state->matches.size()) - 1);
            state->update_preview_for_selection(model->workspace_root);
          }
          return true;
        }
        if (event == Event::ArrowUp) {
          state->cancel_ctrl_chord();
          if (!state->matches.empty()) {
            state->selected = std::max(0, state->selected - 1);
            state->update_preview_for_selection(model->workspace_root);
          }
          return true;
        }
        if (event_is_ctrl_p(event)) {
          if (!state->matches.empty()) {
            state->selected =
                (state->selected + 1) % static_cast<int>(state->matches.size());
            state->ctrl_chord_active = true;
            state->update_preview_for_selection(model->workspace_root);
          }
          return true;
        }
        if (event == Event::Backspace) {
          state->cancel_ctrl_chord();
          if (!state->query.empty()) {
            state->query.pop_back();
            state->selected = 0;
            state->mark_matches_dirty();
            state->refresh_matches(workspace);
            state->update_preview_for_selection(model->workspace_root);
          }
          return true;
        }
        if (event.is_character()) {
          state->cancel_ctrl_chord();
          const std::string ch = event.character();
          if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
              static_cast<unsigned char>(ch[0]) < 127) {
            state->query += ch;
            state->selected = 0;
            state->mark_matches_dirty();
            state->refresh_matches(workspace);
            state->update_preview_for_selection(model->workspace_root);
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
        if (state->preview_requested_path.empty() && !state->matches.empty()) {
          state->update_preview_for_selection(model->workspace_root);
        }

        std::string input_line = state->query;
        input_line.push_back('_');

        Elements matches;
        const int start = std::max(
            0, std::min(state->selected,
                        std::max(0, static_cast<int>(state->matches.size()) - kMaxMatchRows)));
        const int end =
            std::min(static_cast<int>(state->matches.size()), start + kMaxMatchRows);
        for (int i = start; i < end; ++i) {
          const auto& match = state->matches[static_cast<std::size_t>(i)];
          const std::string label =
              picker_display_path(match.path, model->workspace_root);
          matches.push_back(render_fuzzy_path_label(label, match.match_indices,
                                                    i == state->selected, kMatchTextWidth));
        }
        if (matches.empty()) {
          const bool scanning = indexer != nullptr && indexer->scanning();
          const std::string empty_label = state->query.empty()
                                              ? i18n::tr("picker.file.no_open_files")
                                              : (scanning ? i18n::tr("common.indexing")
                                                          : i18n::tr("common.no_matches"));
          matches.push_back(text(empty_label) | color(theme::Muted()));
        }

        const FilePickerPreviewData preview = state->preview.snapshot();
        const std::string title =
            state->query.empty() ? i18n::tr("picker.file.open_files")
                                 : i18n::tr("picker.file.search");

        Element left_pane = vbox({
                               ModalInputLine(input_line),
                               separator(),
                               vbox(std::move(matches)) | frame | vscroll_indicator |
                                   bgcolor(theme::PanelBg()),
                           }) |
                           size(WIDTH, EQUAL, kLeftPaneWidth) | size(HEIGHT, EQUAL, kPaneHeight + 2);

        Element right_pane = render_preview_panel(preview, model->workspace_root);

        Element dialog = ModalWindow(
            text(title) | color(theme::Accent()),
            hbox({
                std::move(left_pane),
                separatorCharacter("│") | color(theme::AccentDim()),
                std::move(right_pane),
            }) | size(WIDTH, EQUAL, kModalWidth) | size(HEIGHT, EQUAL, kPaneHeight + 3));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
