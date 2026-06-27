#pragma once

#include <string>
#include <vector>

#include "ftxui/screen/box.hpp"

namespace tgdb {

struct BrowserEntry {
  std::string name;
  std::string path;
  bool is_directory = false;
  bool is_parent = false;
};

struct PathBrowserState {
  std::string browser_path;
  std::string browser_loaded_path;
  std::string launch_root;
  std::vector<BrowserEntry> entries;
  int selected = 0;
  int browser_list_start = 0;
  ftxui::Box browser_list_box;

  void reset(const std::string& start_path);
  void reload_browser_entries(bool reset_selection);
  void ensure_browser_entries();
};

std::string canonical_browser_root(const std::string& path);
bool is_regular_file_path(const std::string& path);
bool is_directory_path(const std::string& path);

}  // namespace tgdb
