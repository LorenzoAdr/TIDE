#pragma once

#include <functional>
#include <string>

namespace tgdb {

bool is_pdf_path(const std::string& path);

struct PdfLaunchResult {
  bool ok = false;
  std::string message;
  std::string path;
};

using PdfViewerFinishedCallback = std::function<void(const PdfLaunchResult&)>;

void launch_pdf_viewer_async(const std::string& absolute_path,
                             PdfViewerFinishedCallback on_finished = {});

}  // namespace tgdb
