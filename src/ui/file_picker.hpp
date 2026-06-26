#pragma once

#include <string>
#include <vector>

#include "app/debug_model.hpp"
#include "ftxui/component/component_base.hpp"

namespace tgdb {

struct FilePickerState {
  bool open = false;
  std::string query;
  std::vector<std::string> all_files;
  std::vector<std::string> matches;
  int selected = 0;

  void ensure_indexed(const std::string& workspace_root);
  void refresh_matches();
  void open_file(DebugModel* model, int index);
};

ftxui::Component MakeFilePickerOverlay(ftxui::Component main, DebugModel* model,
                                      FilePickerState* state);

}  // namespace tgdb
