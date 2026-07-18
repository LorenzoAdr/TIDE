#include "util/process_list.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace tuide {

namespace {

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool contains_insensitive(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  return to_lower(haystack).find(to_lower(needle)) != std::string::npos;
}

std::string read_file_trimmed(const fs::path& path) {
  std::ifstream input(path);
  if (!input) {
    return "";
  }
  std::string content((std::istreambuf_iterator<char>(input)),
                      std::istreambuf_iterator<char>());
  while (!content.empty() && (content.back() == '\0' || content.back() == '\n')) {
    content.pop_back();
  }
  for (char& c : content) {
    if (c == '\0') {
      c = ' ';
    }
  }
  return content;
}

}  // namespace

std::vector<ProcessEntry> list_processes() {
  std::vector<ProcessEntry> processes;
  std::error_code ec;
  const fs::path proc{"/proc"};
  if (!fs::exists(proc, ec)) {
    return processes;
  }

  for (const auto& entry : fs::directory_iterator(proc, ec)) {
    if (ec) {
      break;
    }
    const std::string name = entry.path().filename().string();
    if (name.empty() || !std::isdigit(static_cast<unsigned char>(name[0]))) {
      continue;
    }

    int pid = 0;
    try {
      pid = std::stoi(name);
    } catch (...) {
      continue;
    }

    ProcessEntry info;
    info.pid = pid;
    info.name = read_file_trimmed(entry.path() / "comm");
    info.cmdline = read_file_trimmed(entry.path() / "cmdline");
    if (info.name.empty() && !info.cmdline.empty()) {
      const auto space = info.cmdline.find(' ');
      info.name = space == std::string::npos ? info.cmdline
                                             : info.cmdline.substr(0, space);
    }
    if (info.name.empty()) {
      continue;
    }
    processes.push_back(std::move(info));
  }

  return processes;
}

std::vector<ProcessEntry> filter_processes(const std::vector<ProcessEntry>& all,
                                           const std::string& query) {
  std::vector<ProcessEntry> matches;
  const std::string pid_query = query;
  for (const auto& entry : all) {
    if (contains_insensitive(entry.name, query) ||
        contains_insensitive(entry.cmdline, query) ||
        (!pid_query.empty() &&
         std::to_string(entry.pid).find(pid_query) != std::string::npos)) {
      matches.push_back(entry);
    }
  }
  return matches;
}

}  // namespace tuide
