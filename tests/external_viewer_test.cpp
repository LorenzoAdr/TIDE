#include "util/external_viewer.hpp"

#include <cstdlib>
#include <iostream>
#include <unistd.h>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  expect(tuide::is_pdf_path("/tmp/report.PDF"), "pdf extension case insensitive");
  expect(!tuide::is_pdf_path("/tmp/report.txt"), "non-pdf rejected");
  expect(tuide::is_markdown_path("/tmp/README.md"), "markdown extension");
  expect(!tuide::is_markdown_path("/tmp/readme.txt"), "non-markdown rejected");
  expect(tuide::is_html_path("/tmp/index.HTML"), "html extension case insensitive");
  expect(tuide::is_html_path("/tmp/index.htm"), "htm extension");
  expect(tuide::is_html_path("/tmp/index.xhtml"), "xhtml extension");
  expect(!tuide::is_html_path("/tmp/index.txt"), "non-html rejected");
  expect(tuide::is_browser_preview_path("/tmp/README.md"), "markdown browser preview");
  expect(tuide::is_browser_preview_path("/tmp/index.html"), "html browser preview");

  bool finished = false;
  tuide::PdfLaunchResult result;
  tuide::launch_pdf_viewer_async("/tmp/tuide-nonexistent-test.pdf",
                                [&](const tuide::PdfLaunchResult& launch) {
                                  result = launch;
                                  finished = true;
                                });

  for (int i = 0; i < 200 && !finished; ++i) {
    usleep(10000);
  }
  expect(finished, "async callback invoked");
  expect(!result.ok, "missing pdf should fail gracefully");

  bool md_finished = false;
  tuide::MarkdownLaunchResult md_result;
  tuide::launch_markdown_browser_async("/tmp/tuide-nonexistent-test.md",
                                       [&](const tuide::MarkdownLaunchResult& launch) {
                                         md_result = launch;
                                         md_finished = true;
                                       });
  for (int i = 0; i < 200 && !md_finished; ++i) {
    usleep(10000);
  }
  expect(md_finished, "markdown async callback invoked");
  expect(!md_result.ok, "missing markdown should fail gracefully");

  bool html_finished = false;
  tuide::HtmlLaunchResult html_result;
  tuide::launch_html_browser_async("/tmp/tuide-nonexistent-test.html",
                                   [&](const tuide::HtmlLaunchResult& launch) {
                                     html_result = launch;
                                     html_finished = true;
                                   });
  for (int i = 0; i < 200 && !html_finished; ++i) {
    usleep(10000);
  }
  expect(html_finished, "html async callback invoked");
  expect(!html_result.ok, "missing html should fail gracefully");

  std::cout << "external_viewer_test: OK\n";
  return 0;
}
