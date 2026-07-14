#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"
#include "ui/file_picker_preview.hpp"

namespace tgdb {

ftxui::Element RenderFilePreviewPanel(const FilePickerPreviewData& preview,
                                      const std::string& workspace_root, int pane_width,
                                      int pane_height, int max_rows);

}  // namespace tgdb
