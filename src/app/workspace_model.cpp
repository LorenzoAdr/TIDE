#include "app/workspace_model.hpp"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace tgdb {

bool WorkspaceModel::load_file(const std::string& absolute_path) {
  buffer.lines.clear();
  buffer.path = absolute_path;
  buffer.cursor_line = 0;
  buffer.cursor_col = 0;
  buffer.scroll = 0;
  buffer.dirty = false;

  if (absolute_path.empty()) {
    buffer.lines.push_back("Abre un archivo del explorador o Ctrl+P.");
    return false;
  }

  std::ifstream input(absolute_path);
  if (!input) {
    buffer.lines.push_back("No se pudo abrir: " + absolute_path);
    return false;
  }

  std::string line;
  while (std::getline(input, line)) {
    buffer.lines.push_back(line);
  }
  if (buffer.lines.empty()) {
    buffer.lines.push_back("");
  }
  active_file = absolute_path;
  buffer.view_token++;
  return true;
}

bool WorkspaceModel::save_buffer() {
  if (buffer.path.empty() || buffer.lines.empty()) {
    return false;
  }
  std::ofstream output(buffer.path, std::ios::trunc);
  if (!output) {
    return false;
  }
  for (std::size_t i = 0; i < buffer.lines.size(); ++i) {
    output << buffer.lines[i];
    if (i + 1 < buffer.lines.size()) {
      output << '\n';
    }
  }
  buffer.dirty = false;
  status_message = "Guardado: " + fs::path(buffer.path).filename().string();
  return true;
}

void WorkspaceModel::open_relative(const std::string& relative_path) {
  if (root.empty() || relative_path.empty()) {
    return;
  }
  std::error_code ec;
  const auto absolute =
      fs::weakly_canonical(fs::path(root) / relative_path, ec);
  if (ec || !fs::is_regular_file(absolute, ec)) {
    return;
  }
  load_file(absolute.string());
}

void WorkspaceModel::ensure_buffer() {
  if (buffer.lines.empty() && !active_file.empty()) {
    load_file(active_file);
  } else if (buffer.lines.empty()) {
    buffer.lines.push_back("Abre un archivo del explorador o Ctrl+P.");
  }
}

}  // namespace tgdb
