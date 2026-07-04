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
  expect(tgdb::is_pdf_path("/tmp/report.PDF"), "pdf extension case insensitive");
  expect(!tgdb::is_pdf_path("/tmp/report.txt"), "non-pdf rejected");

  bool finished = false;
  tgdb::PdfLaunchResult result;
  tgdb::launch_pdf_viewer_async("/tmp/tgdb-nonexistent-test.pdf",
                                [&](const tgdb::PdfLaunchResult& launch) {
                                  result = launch;
                                  finished = true;
                                });

  for (int i = 0; i < 200 && !finished; ++i) {
    usleep(10000);
  }
  expect(finished, "async callback invoked");
  expect(!result.ok, "missing pdf should fail gracefully");

  std::cout << "external_viewer_test: OK\n";
  return 0;
}
