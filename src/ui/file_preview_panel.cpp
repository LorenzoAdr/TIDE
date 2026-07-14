#include "ui/file_preview_panel.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/dom/elements.hpp"
#include "i18n/tr.hpp"
#include "indexer/index_rules.hpp"
#include "parser/tree_sitter_document.hpp"
#include "parser/tree_sitter_highlight.hpp"
#include "parser/tree_sitter_service.hpp"
#include "ui/theme.hpp"
#include "util/line_source.hpp"
#include "util/syntax_highlight.hpp"
#include "util/build_file_highlight.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

std::string preview_display_path(const std::string& path, const std::string& workspace_root) {
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

void ensure_preview_viewport_highlights(const FilePickerPreviewData& preview, int max_rows) {
  if (!preview.use_tree_sitter || preview.highlight_lines.empty()) {
    return;
  }
  const std::string source = join_editor_lines(preview.highlight_lines);
  if (tree_sitter_service().document_highlights_ready(preview.path, source)) {
    return;
  }

  std::vector<int> line_indices;
  const int visible = std::min(static_cast<int>(preview.lines.size()), max_rows);
  line_indices.reserve(static_cast<std::size_t>(visible));
  for (int i = 0; i < visible; ++i) {
    line_indices.push_back(preview.first_line_number + i - 1);
  }
  if (!line_indices.empty()) {
    tree_sitter_service().ensure_viewport_preview(preview.path, source, line_indices);
  }
}

Element render_preview_line(const FilePickerPreviewData& preview, const std::string& line,
                            int line_index_0based) {
  if (preview.build_file_kind != BuildFileKind::kNone) {
    return HighlightBuildFileLine(preview.build_file_kind, line);
  }
  if (!preview.use_tree_sitter || preview.highlight_lines.empty() || line_index_0based < 0 ||
      line_index_0based >= static_cast<int>(preview.highlight_lines.size())) {
    return HighlightCodeLineLite(line);
  }

  const std::string source = join_editor_lines(preview.highlight_lines);

  if (const LineHighlights* viewport_hl = tree_sitter_service().viewport_preview_line(
          preview.path, source, line_index_0based)) {
    if (!viewport_hl->spans.empty()) {
      return HighlightTreeSitterLine(line, line_index_0based, *viewport_hl, -1, {}, 0);
    }
  }

  if (const std::vector<LineHighlights>* all_hl =
          tree_sitter_service().highlights_for(preview.path, source)) {
    if (line_index_0based < static_cast<int>(all_hl->size())) {
      const LineHighlights& line_hl = (*all_hl)[static_cast<std::size_t>(line_index_0based)];
      if (!line_hl.spans.empty()) {
        return HighlightTreeSitterLine(line, line_index_0based, line_hl, -1, {}, 0);
      }
    }
  }

  return HighlightCodeLineLite(line);
}

}  // namespace

Element RenderFilePreviewPanel(const FilePickerPreviewData& preview,
                               const std::string& workspace_root, int pane_width,
                               int pane_height, int max_rows) {
  const std::string header =
      preview.path.empty() ? " " : preview_display_path(preview.path, workspace_root);

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
      ensure_preview_viewport_highlights(preview, max_rows);

      const int line_count = static_cast<int>(preview.lines.size());
      const int end = std::min(line_count, max_rows);
      for (int i = 0; i < end; ++i) {
        const int file_line = preview.first_line_number + i;
        const int line_index_0based = file_line - 1;
        const std::string& line = preview.lines[static_cast<std::size_t>(i)];
        const std::string line_no = std::to_string(file_line);
        const int gutter = std::max(4, static_cast<int>(line_no.size()) + 1);
        const bool highlighted =
            preview.highlight_line > 0 && file_line == preview.highlight_line;
        Element code = render_preview_line(preview, line, line_index_0based);
        if (highlighted) {
          code = code | bgcolor(theme::SelectionBg()) | bold;
        }
        Element row = hbox({
            text(line_no + std::string(static_cast<std::size_t>(gutter - line_no.size()), ' ')) |
                color(highlighted ? theme::Accent() : theme::Muted()) |
                size(WIDTH, EQUAL, gutter),
            code,
        });
        body.push_back(std::move(row));
      }
      if (line_count > max_rows) {
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
         size(WIDTH, EQUAL, pane_width) | size(HEIGHT, EQUAL, pane_height);
}

}  // namespace tgdb
