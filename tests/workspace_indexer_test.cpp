#include <stdexcept>
#include <string>
#include <vector>

#include "indexer/workspace_indexer.hpp"
#include "util/fuzzy_match.hpp"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_rebuild_files_lower() {
  tgdb::IndexSnapshot snapshot;
  snapshot.files = {"Src/Main.cpp", "src/app/Application.cpp"};
  tgdb::rebuild_index_files_lower(&snapshot);
  check(snapshot.files_lower.size() == snapshot.files.size(), "parallel size");
  check(snapshot.files_lower[0] == "src/main.cpp", "first lower");
  check(snapshot.files_lower[1] == "src/app/application.cpp", "second lower");
}

void test_rebuild_file_picker_catalog() {
  tgdb::IndexSnapshot snapshot;
  snapshot.files = {"src/main.cpp", "src/app/application.cpp"};
  tgdb::rebuild_index_file_picker_catalog(&snapshot);
  check(snapshot.file_picker_catalog != nullptr, "catalog exists");
  check(snapshot.file_picker_catalog->size() == 2, "catalog size");
  check((*snapshot.file_picker_catalog)[0].filename == "main.cpp", "first filename");
  check((*snapshot.file_picker_catalog)[0].filename_lower == "main.cpp", "first filename lower");
  check((*snapshot.file_picker_catalog)[0].display_label == "src/main.cpp", "first label");
  check((*snapshot.file_picker_catalog)[1].dir_label == "src/app", "second dir");
}

}  // namespace

int main() {
  test_rebuild_files_lower();
  test_rebuild_file_picker_catalog();
  return 0;
}
