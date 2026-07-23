#include <stdexcept>
#include <string>
#include <vector>

#include "indexer/index_rules.hpp"
#include "indexer/workspace_indexer.hpp"
#include "util/fuzzy_match.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_rebuild_files_lower() {
  tuide::IndexSnapshot snapshot;
  snapshot.files = {"Src/Main.cpp", "src/app/Application.cpp"};
  tuide::rebuild_index_files_lower(&snapshot);
  check(snapshot.files_lower.size() == snapshot.files.size(), "parallel size");
  check(snapshot.files_lower[0] == "src/main.cpp", "first lower");
  check(snapshot.files_lower[1] == "src/app/application.cpp", "second lower");
}

void test_rebuild_file_picker_catalog() {
  tuide::IndexSnapshot snapshot;
  snapshot.files = {"src/main.cpp", "src/app/application.cpp"};
  tuide::rebuild_index_file_picker_catalog(&snapshot);
  check(snapshot.file_picker_catalog != nullptr, "catalog exists");
  check(snapshot.file_picker_catalog->size() == 2, "catalog size");
  check((*snapshot.file_picker_catalog)[0].filename == "main.cpp", "first filename");
  check((*snapshot.file_picker_catalog)[0].filename_lower == "main.cpp", "first filename lower");
  check((*snapshot.file_picker_catalog)[0].display_label == "src/main.cpp", "first label");
  check((*snapshot.file_picker_catalog)[1].dir_label == "src/app", "second dir");
}

void test_file_picker_excludes_binaries_keeps_pdf() {
  check(tuide::is_file_picker_candidate_path("src/main.cpp"), "source kept");
  check(tuide::is_file_picker_candidate_path("docs/manual.pdf"), "pdf kept");
  check(tuide::is_file_picker_candidate_path("docs/MANUAL.PDF"), "pdf case kept");
  check(!tuide::is_file_picker_candidate_path("build/libfoo.a"), "archive excluded");
  check(!tuide::is_file_picker_candidate_path("build/foo.o"), "object excluded");
  check(!tuide::is_file_picker_candidate_path("build/libfoo.so"), "shared lib excluded");
  check(!tuide::is_file_picker_candidate_path("dist/app.tar.gz"), "tar.gz excluded");
  check(!tuide::is_file_picker_candidate_path("dist/app.tgz"), "tgz excluded");

  tuide::IndexSnapshot snapshot;
  snapshot.files = {"src/main.cpp", "build/foo.o", "docs/guide.pdf", "lib/libx.a", "pkg.tar.gz"};
  tuide::rebuild_index_file_picker_catalog(&snapshot);
  check(snapshot.file_picker_catalog != nullptr, "filtered catalog exists");
  check(snapshot.file_picker_catalog->size() == 2, "filtered catalog size");
  check((*snapshot.file_picker_catalog)[0].path == "src/main.cpp", "source in catalog");
  check((*snapshot.file_picker_catalog)[1].path == "docs/guide.pdf", "pdf in catalog");
}

}  // namespace

int main() {
  test_rebuild_files_lower();
  test_rebuild_file_picker_catalog();
  test_file_picker_excludes_binaries_keeps_pdf();
  return 0;
}
