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

}  // namespace

int main() {
  test_rebuild_files_lower();
  return 0;
}
