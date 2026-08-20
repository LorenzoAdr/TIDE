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

void test_index_path_matches_prefix() {
  check(tuide::index_path_matches_prefix("chapters/a.aux", "chapters/a.aux"), "exact");
  check(tuide::index_path_matches_prefix("chapters/a/x.tex", "chapters/a"), "child");
  check(!tuide::index_path_matches_prefix("chapters/ab", "chapters/a"), "sibling prefix");
  check(!tuide::index_path_matches_prefix("chapters", "chapters/a"), "parent");
}

void test_coalesce_file_index_changes_drops_dominated_removes() {
  std::vector<tuide::FileIndexChange> changes;
  {
    tuide::FileIndexChange c;
    c.kind = tuide::FileIndexChangeKind::RemovePrefix;
    c.relative_path = "out/a.aux";
    c.wake_ui = true;
    changes.push_back(c);
  }
  {
    tuide::FileIndexChange c;
    c.kind = tuide::FileIndexChangeKind::RemovePrefix;
    c.relative_path = "out";
    c.wake_ui = true;
    changes.push_back(c);
  }
  {
    tuide::FileIndexChange c;
    c.kind = tuide::FileIndexChangeKind::RemovePrefix;
    c.relative_path = "out/b.log";
    c.wake_ui = true;
    changes.push_back(c);
  }
  {
    tuide::FileIndexChange c;
    c.kind = tuide::FileIndexChangeKind::Upsert;
    c.relative_path = "main.tex";
    c.absolute_path = "/tmp/main.tex";
    c.wake_ui = true;
    changes.push_back(c);
  }
  {
    tuide::FileIndexChange c;
    c.kind = tuide::FileIndexChangeKind::RemovePrefix;
    c.relative_path = "tmp.aux";
    c.wake_ui = true;
    changes.push_back(c);
  }

  const auto out = tuide::coalesce_file_index_changes(std::move(changes));
  check(out.size() == 3, "coalesced size");
  check(out[0].kind == tuide::FileIndexChangeKind::RemovePrefix, "first remove");
  check(out[0].relative_path == "out", "dominated children dropped");
  check(out[1].kind == tuide::FileIndexChangeKind::Upsert, "upsert preserved");
  check(out[1].relative_path == "main.tex", "upsert path");
  check(out[2].kind == tuide::FileIndexChangeKind::RemovePrefix, "trailing remove");
  check(out[2].relative_path == "tmp.aux", "trailing path");
}

void test_fs_change_debounce_constants() {
  check(tuide::kIndexerFsChangeDebounceMs > 0, "debounce configured");
  check(tuide::kIndexerFsChangeMaxDebounceMs > tuide::kIndexerFsChangeDebounceMs, "max > quiet");
}

}  // namespace

int main() {
  test_rebuild_files_lower();
  test_rebuild_file_picker_catalog();
  test_file_picker_excludes_binaries_keeps_pdf();
  test_index_path_matches_prefix();
  test_coalesce_file_index_changes_drops_dominated_removes();
  test_fs_change_debounce_constants();
  return 0;
}
