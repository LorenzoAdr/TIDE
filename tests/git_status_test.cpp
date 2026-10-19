#include "git/git_status.hpp"

#include <cassert>
#include <iostream>

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::abort();
  }
}

}  // namespace

int main() {
  {
    const tuide::GitStatusSnapshot snap =
        tuide::parse_git_status_porcelain(" M src/a.cpp\n?? src/new.cpp\nA  src/b.cpp\n");
    expect(snap.entries.size() == 3, "expected 3 entries");
    expect(snap.conflicted_count == 0, "no conflicts expected");
    expect(!snap.entries[0].is_conflicted(), "modified entry should not be conflicted");
  }

  // Todos los códigos "unmerged" documentados en git-status(1).
  const char* unmerged_codes[] = {"DD", "AU", "UD", "UA", "DU", "AA", "UU"};
  for (const char* code : unmerged_codes) {
    const std::string line = std::string(code) + " conflicted.txt\n";
    const tuide::GitStatusSnapshot snap = tuide::parse_git_status_porcelain(line);
    expect(snap.entries.size() == 1, "expected 1 entry");
    expect(snap.entries[0].is_conflicted(), (std::string("expected conflict for ") + code).c_str());
    expect(snap.entries[0].path == "conflicted.txt", "path should be parsed");
    expect(snap.conflicted_count == 1, "conflicted_count should be 1");
    expect(snap.staged_count == 0 && snap.unstaged_count == 0,
           "conflicted entries should not count as staged/unstaged");
  }

  {
    // Un renombrado normal (R ) no debe confundirse con un conflicto.
    const tuide::GitStatusSnapshot snap =
        tuide::parse_git_status_porcelain("R  old.txt -> new.txt\n");
    expect(snap.entries.size() == 1, "expected 1 entry");
    expect(!snap.entries[0].is_conflicted(), "rename should not be conflicted");
    expect(snap.entries[0].path == "new.txt", "rename path should resolve to target");
  }

  std::cout << "git_status_test OK\n";
  return 0;
}
