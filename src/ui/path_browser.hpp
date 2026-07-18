#pragma once

#include <string>
#include <vector>

#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"

namespace tuide {

struct BrowserEntry {
  std::string name;
  std::string path;
  bool is_directory = false;
  bool is_parent = false;
};

enum class PathBrowserFilterResult {
  kNotHandled,
  kHandled,
  kClearFilter,
};

struct PathBrowserState {
  std::string browser_path;
  std::string browser_loaded_path;
  std::string launch_root;
  std::vector<BrowserEntry> entries;
  std::string filter_query;
  int selected = 0;
  int browser_list_start = 0;
  ftxui::Box browser_list_box;

  void reset(const std::string& start_path);
  void reload_browser_entries(bool reset_selection);
  void ensure_browser_entries();
  void clear_filter();
  void apply_filter();
  PathBrowserFilterResult handle_filter_input(const ftxui::Event& event);
  bool handle_list_navigation(const ftxui::Event& event, int page_size = 12);
};

std::string canonical_browser_root(const std::string& path);
bool is_regular_file_path(const std::string& path);
bool is_directory_path(const std::string& path);
std::vector<BrowserEntry> list_directory_entries(const std::string& path);

}  // namespace tuide
