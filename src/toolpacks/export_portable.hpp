#pragma once

#include <string>
#include <vector>

namespace tuide::toolpacks {

struct ExportResult {
  bool ok = false;
  std::string message;
  std::string output_path;
};

// Embed selected toolpacks into a clean core binary.
// source_binary empty => /proc/self/exe
// toolpack_ids empty => all installed
ExportResult export_portable(const std::string& source_binary,
                             const std::string& output_path,
                             const std::vector<std::string>& toolpack_ids);

int run_export_cli(int argc, char** argv);

}  // namespace tuide::toolpacks
