#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "app/workspace_model.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/screen/box.hpp"
#include "ui/main_layout.hpp"

namespace tuide {

enum class OpenFileConfirmMode { Closed, LargeFile, BinaryWarning, TooLarge };

struct OpenFileConfirmState {
  OpenFileConfirmMode mode = OpenFileConfirmMode::Closed;
  // LargeFile: 0 = virtualize, 1 = load full, 2 = cancel.
  // Other modes: 0 = OK.
  int selected = 0;
  std::string path;
  std::string display_name;
  std::uintmax_t size_bytes = 0;
  int line = 0;
  int col = 0;
  bool has_position = false;
  ftxui::Box yes_box;
  ftxui::Box load_full_box;
  ftxui::Box no_box;

  bool is_open() const { return mode != OpenFileConfirmMode::Closed; }

  void show_binary_warning(const std::string& absolute_path, const std::string& display_name);
  void show_too_large_warning(const std::string& absolute_path, const std::string& display_name,
                              std::uintmax_t size_bytes);
  void request_large_confirm(const std::string& absolute_path, std::uintmax_t size_bytes);
  void close();
};

ftxui::Component MakeOpenFileConfirmOverlay(
    ftxui::Component main, OpenFileConfirmState* state, MainLayoutState* layout_state,
    WorkspaceModel* workspace, std::function<void(const std::string& path, int line, int col)> on_opened);

}  // namespace tuide
