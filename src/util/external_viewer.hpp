#pragma once

#include <functional>
#include <string>

namespace tuide {

bool is_pdf_path(const std::string& path);

struct PdfLaunchResult {
  bool ok = false;
  std::string message;
  std::string path;
};

using PdfViewerFinishedCallback = std::function<void(const PdfLaunchResult&)>;

void launch_pdf_viewer_async(const std::string& absolute_path,
                             PdfViewerFinishedCallback on_finished = {});

bool is_markdown_path(const std::string& path);

struct MarkdownLaunchResult {
  bool ok = false;
  std::string message;
  std::string path;
};

using MarkdownViewerFinishedCallback = std::function<void(const MarkdownLaunchResult&)>;

void launch_markdown_browser_async(const std::string& absolute_path,
                                   MarkdownViewerFinishedCallback on_finished = {});

}  // namespace tuide
