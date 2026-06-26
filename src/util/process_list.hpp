#pragma once

#include <string>
#include <vector>

namespace tgdb {

struct ProcessEntry {
  int pid = 0;
  std::string name;
  std::string cmdline;
};

std::vector<ProcessEntry> list_processes();
std::vector<ProcessEntry> filter_processes(const std::vector<ProcessEntry>& all,
                                           const std::string& query);

}  // namespace tgdb
