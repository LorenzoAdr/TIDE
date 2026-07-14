#include "ui/path_browser.hpp"

#include <algorithm>
#include <filesystem>

#include "ftxui/component/event.hpp"
#include "util/fuzzy_match.hpp"

namespace fs = std::filesystem;

namespace tgdb {

using ftxui::Event;

std::string canonical_browser_root(const std::string& path) {
  if (path.empty()) {
    std::error_code ec;
    return fs::current_path(ec).string();
  }
  std::error_code ec;
  const auto canonical = fs::weakly_canonical(fs::path(path), ec);
  return ec ? path : canonical.string();
}

bool is_regular_file_path(const std::string& path) {
  std::error_code ec;
  return fs::is_regular_file(path, ec);
}

bool is_directory_path(const std::string& path) {
  std::error_code ec;
  return fs::is_directory(path, ec);
}

void PathBrowserState::reset(const std::string& start_path) {
  browser_path = canonical_browser_root(start_path);
  browser_loaded_path.clear();
  entries.clear();
  filter_query.clear();
  selected = 0;
  browser_list_start = 0;
}

void PathBrowserState::reload_browser_entries(bool reset_selection) {
  entries.clear();
  if (reset_selection) {
    filter_query.clear();
    selected = 0;
    browser_list_start = 0;
  }
  std::error_code ec;
  fs::path current(browser_path);
  if (!fs::exists(current, ec)) {
    browser_path = canonical_browser_root(launch_root);
    current = fs::path(browser_path);
  }
  browser_path = fs::weakly_canonical(current, ec).string();
  if (ec) {
    browser_path = current.string();
  }
  browser_loaded_path = browser_path;

  if (current.has_parent_path()) {
    BrowserEntry parent;
    parent.name = "..";
    parent.path = current.parent_path().string();
    parent.is_directory = true;
    parent.is_parent = true;
    entries.push_back(std::move(parent));
  }

  std::vector<BrowserEntry> dirs;
  std::vector<BrowserEntry> files;
  for (const auto& entry : fs::directory_iterator(current, ec)) {
    if (ec) {
      break;
    }
    BrowserEntry row;
    row.name = entry.path().filename().string();
    if (row.name.empty() || row.name[0] == '.') {
      continue;
    }
    row.path = entry.path().string();
    if (entry.is_directory(ec)) {
      row.is_directory = true;
      dirs.push_back(std::move(row));
    } else if (entry.is_regular_file(ec)) {
      row.is_directory = false;
      files.push_back(std::move(row));
    }
  }

  auto by_name = [](const BrowserEntry& a, const BrowserEntry& b) {
    return a.name < b.name;
  };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  entries.insert(entries.end(), dirs.begin(), dirs.end());
  entries.insert(entries.end(), files.begin(), files.end());

  selected = std::max(
      0, std::min(selected, std::max(0, static_cast<int>(entries.size()) - 1)));
}

void PathBrowserState::ensure_browser_entries() {
  if (browser_loaded_path == browser_path && !entries.empty()) {
    return;
  }
  reload_browser_entries(true);
}

void PathBrowserState::clear_filter() {
  filter_query.clear();
}

void PathBrowserState::apply_filter() {
  if (filter_query.empty()) {
    return;
  }

  const std::string query_lower = fuzzy_to_lower(filter_query);
  int best_idx = -1;
  int best_score = -1;
  std::size_t best_len = std::string::npos;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    const BrowserEntry& entry = entries[static_cast<std::size_t>(i)];
    if (entry.is_parent) {
      continue;
    }
    const std::string name_lower = fuzzy_to_lower(entry.name);
    const FuzzyMatchResult result = fuzzy_match_cached(entry.name, name_lower, query_lower);
    if (!result.matched) {
      continue;
    }
    if (result.score > best_score ||
        (result.score == best_score && entry.name.size() < best_len)) {
      best_score = result.score;
      best_idx = i;
      best_len = entry.name.size();
    }
  }
  if (best_idx >= 0) {
    selected = best_idx;
  }
}

PathBrowserFilterResult PathBrowserState::handle_filter_input(const Event& event) {
  if (event == Event::Escape) {
    if (!filter_query.empty()) {
      clear_filter();
      return PathBrowserFilterResult::kClearFilter;
    }
    return PathBrowserFilterResult::kNotHandled;
  }
  if (event == Event::Backspace) {
    if (!filter_query.empty()) {
      filter_query.pop_back();
      apply_filter();
    }
    return PathBrowserFilterResult::kHandled;
  }
  if (event.is_character()) {
    const std::string ch = event.character();
    if (ch.size() == 1 && static_cast<unsigned char>(ch[0]) >= 32 &&
        static_cast<unsigned char>(ch[0]) < 127) {
      filter_query += ch;
      apply_filter();
      return PathBrowserFilterResult::kHandled;
    }
  }
  return PathBrowserFilterResult::kNotHandled;
}

bool PathBrowserState::handle_list_navigation(const Event& event, int page_size) {
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    clear_filter();
    selected = std::min(selected + 1, std::max(0, static_cast<int>(entries.size()) - 1));
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    clear_filter();
    selected = std::max(0, selected - 1);
    return true;
  }
  if (event == Event::PageDown) {
    clear_filter();
    selected = std::min(selected + page_size,
                        std::max(0, static_cast<int>(entries.size()) - 1));
    return true;
  }
  if (event == Event::PageUp) {
    clear_filter();
    selected = std::max(0, selected - page_size);
    return true;
  }
  return false;
}

}  // namespace tgdb
