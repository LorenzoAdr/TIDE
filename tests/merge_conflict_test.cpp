#include "git/merge_conflict.hpp"

#include <cassert>
#include <iostream>

namespace {

using tuide::ConflictHunk;
using tuide::ConflictParseResult;
using tuide::ConflictSide;

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::abort();
  }
}

std::vector<std::string> lines(std::initializer_list<const char*> items) {
  return std::vector<std::string>(items.begin(), items.end());
}

void expect_lines(const std::vector<std::string>& actual,
                  std::initializer_list<const char*> expected, const char* message) {
  const std::vector<std::string> exp(expected.begin(), expected.end());
  if (actual != exp) {
    std::cerr << "FAIL: " << message << " (got " << actual.size() << " lines, expected "
              << exp.size() << ")\n";
    for (const auto& l : actual) std::cerr << "  got: " << l << '\n';
    std::abort();
  }
}

}  // namespace

int main() {
  // Conflicto simple de 2 vías (sin base diff3).
  {
    const auto src = lines({
        "void f() {",
        "<<<<<<< HEAD",
        "  return 1;",
        "=======",
        "  return 2;",
        ">>>>>>> feature",
        "}",
    });
    const ConflictParseResult result = tuide::parse_conflict_markers(src);
    expect(result.has_conflicts(), "should find one hunk");
    expect(result.hunks.size() == 1, "exactly one hunk");
    const ConflictHunk& hunk = result.hunks[0];
    expect(hunk.start_line == 1, "start_line at <<<<<<<");
    expect(hunk.end_line == 5, "end_line at >>>>>>>");
    expect(!hunk.has_base, "no base section");
    expect(hunk.ours_label == "HEAD", "ours label parsed");
    expect(hunk.theirs_label == "feature", "theirs label parsed");

    expect_lines(tuide::conflict_hunk_side_lines(src, hunk, ConflictSide::kOurs),
                {"  return 1;"}, "ours side");
    expect_lines(tuide::conflict_hunk_side_lines(src, hunk, ConflictSide::kTheirs),
                {"  return 2;"}, "theirs side");
    expect(tuide::conflict_hunk_side_lines(src, hunk, ConflictSide::kBase).empty(),
           "base side empty when no base");
    expect_lines(tuide::conflict_hunk_both_lines(src, hunk), {"  return 1;", "  return 2;"},
                "both sides concatenated");
  }

  // Estilo diff3 con sección base ("|||||||").
  {
    const auto src = lines({
        "<<<<<<< HEAD",
        "ours line",
        "||||||| merged common ancestors",
        "base line",
        "=======",
        "theirs line",
        ">>>>>>> feature",
    });
    const ConflictParseResult result = tuide::parse_conflict_markers(src);
    expect(result.hunks.size() == 1, "diff3 hunk parsed");
    const ConflictHunk& hunk = result.hunks[0];
    expect(hunk.has_base, "has_base true for diff3 style");
    expect_lines(tuide::conflict_hunk_side_lines(src, hunk, ConflictSide::kBase), {"base line"},
                "base side content");
    expect_lines(tuide::conflict_hunk_side_lines(src, hunk, ConflictSide::kOurs), {"ours line"},
                "ours side content with base present");
  }

  // Múltiples hunks + navegación siguiente/anterior con vuelta al principio/final.
  {
    const auto src = lines({
        "a",
        "<<<<<<< HEAD",
        "1",
        "=======",
        "2",
        ">>>>>>> feature",
        "b",
        "<<<<<<< HEAD",
        "3",
        "=======",
        "4",
        ">>>>>>> feature",
        "c",
    });
    const ConflictParseResult result = tuide::parse_conflict_markers(src);
    expect(result.hunks.size() == 2, "two hunks found");

    expect(tuide::find_next_conflict_hunk(result.hunks, 0) == 0, "next from line 0 -> hunk 0");
    expect(tuide::find_next_conflict_hunk(result.hunks, 1) == 1, "next from inside hunk 0 -> hunk 1");
    expect(tuide::find_next_conflict_hunk(result.hunks, 12) == 0, "next wraps to hunk 0 at EOF");

    expect(tuide::find_prev_conflict_hunk(result.hunks, 12) == 1, "prev from end -> hunk 1");
    expect(tuide::find_prev_conflict_hunk(result.hunks, 7) == 0, "prev from inside hunk 1 -> hunk 0");
    expect(tuide::find_prev_conflict_hunk(result.hunks, 0) == 1, "prev wraps to last hunk");

    expect(tuide::conflict_hunk_at_line(result.hunks, 3) == 0, "line inside hunk 0");
    expect(tuide::conflict_hunk_at_line(result.hunks, 9) == 1, "line inside hunk 1");
    expect(tuide::conflict_hunk_at_line(result.hunks, 6) == -1, "line between hunks matches none");
  }

  // Clasificación por banda visual (ours/base/theirs), incluyendo diff3.
  {
    const auto src = lines({
        "a",
        "<<<<<<< HEAD",
        "ours",
        "||||||| base",
        "base",
        "=======",
        "theirs",
        ">>>>>>> feature",
        "b",
    });
    const auto hunks = tuide::parse_conflict_markers(src).hunks;
    expect(tuide::conflict_line_band(hunks, 0) == tuide::ConflictLineBand::kNone, "line 0 not in any band");
    expect(tuide::conflict_line_band(hunks, 1) == tuide::ConflictLineBand::kOurs, "<<<<<<< line is ours band");
    expect(tuide::conflict_line_band(hunks, 2) == tuide::ConflictLineBand::kOurs, "ours content is ours band");
    expect(tuide::conflict_line_band(hunks, 3) == tuide::ConflictLineBand::kBase, "||||||| line is base band");
    expect(tuide::conflict_line_band(hunks, 4) == tuide::ConflictLineBand::kBase, "base content is base band");
    expect(tuide::conflict_line_band(hunks, 5) == tuide::ConflictLineBand::kTheirs, "======= line is theirs band");
    expect(tuide::conflict_line_band(hunks, 6) == tuide::ConflictLineBand::kTheirs, "theirs content is theirs band");
    expect(tuide::conflict_line_band(hunks, 7) == tuide::ConflictLineBand::kTheirs, ">>>>>>> line is theirs band");
    expect(tuide::conflict_line_band(hunks, 8) == tuide::ConflictLineBand::kNone, "line 8 not in any band");
  }

  // Sin marcadores de cierre: el hunk se descarta en vez de reportarse a medias.
  {
    const auto src = lines({
        "<<<<<<< HEAD",
        "ours only, never closed",
    });
    const ConflictParseResult result = tuide::parse_conflict_markers(src);
    expect(!result.has_conflicts(), "unterminated hunk must be discarded");
  }

  // Un "<<<<<<<" dentro de contenido real (ej. un diff citado) con menos de 7
  // caracteres no debe confundirse con un marcador.
  {
    const auto src = lines({"<<<<<< not a marker", "plain text"});
    const ConflictParseResult result = tuide::parse_conflict_markers(src);
    expect(!result.has_conflicts(), "short prefix must not match marker");
  }

  std::cout << "merge_conflict_test OK\n";
  return 0;
}
