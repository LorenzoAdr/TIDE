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
// toolpack_ids empty + core_only=false => all installed active (error if none)
// core_only=true => AppImage/AppDir with no toolpacks (official slim core)
ExportResult export_portable(const std::string& source_binary,
                             const std::string& output_path,
                             const std::vector<std::string>& toolpack_ids,
                             ExportFormat format = ExportFormat::kAppImage,
                             ProgressFn on_progress = {},
                             bool core_only = false);

int run_export_cli(int argc, char** argv);

}  // namespace tuide::toolpacks
