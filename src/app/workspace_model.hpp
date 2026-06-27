#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "editor/editor_state.hpp"

namespace tgdb {

struct WorkspaceModel {
  std::string root;
  std::string active_file;
  EditorBuffer buffer;
  std::string status_message = "Selecciona un workspace";

  bool load_file(const std::string& absolute_path);
  bool save_buffer();
  void open_relative(const std::string& relative_path);
  void ensure_buffer();
};

}  // namespace tgdb
