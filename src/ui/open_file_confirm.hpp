#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tgdb {

enum class OpenFileConfirmMode { Closed, LargeFile, BinaryWarning };

struct OpenFileConfirmState {
  OpenFileConfirmMode mode = OpenFileConfirmMode::Closed;
  int selected = 0;  // 0 = Sí/OK, 1 = No
  std::string path;
  std::string display_name;
  std::uintmax_t size_bytes = 0;
  int line = 0;
  int col = 0;
  bool has_position = false;
  ftxui::Box yes_box;
  ftxui::Box no_box;

  bool is_open() const { return mode != OpenFileConfirmMode::Closed; }

  void show_binary_warning(const std::string& absolute_path, const std::string& display_name);
  void request_large_confirm(const std::string& absolute_path, std::uintmax_t size_bytes);
  void close();
};

class WorkspaceModel;

ftxui::Component MakeOpenFileConfirmOverlay(
    ftxui::Component main, OpenFileConfirmState* state, MainLayoutState* layout_state,
    WorkspaceModel* workspace, std::function<void(const std::string& path, int line, int col)> on_opened);

}  // namespace tgdb
