#pragma once

#include <string>
#include <vector>

#include "toolpacks/progress.hpp"

namespace tuide::toolpacks {

enum class ExportFormat {
  kAppImage,
  kAppDir,
};

struct ExportResult {
  bool ok = false;
  std::string message;
  std::string output_path;
};

// Export selected toolpacks with a clean core as AppDir and/or AppImage.
// source_binary empty => /proc/self/exe
// toolpack_ids empty => all installed active
ExportResult export_portable(const std::string& source_binary,
                             const std::string& output_path,
                             const std::vector<std::string>& toolpack_ids,
                             ExportFormat format = ExportFormat::kAppImage,
                             ProgressFn on_progress = {});

int run_export_cli(int argc, char** argv);

}  // namespace tuide::toolpacks
