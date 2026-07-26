#include "app/recent_projects.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

void expect_eq(const std::string& actual, const std::string& expected, const char* message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got '" << actual << "', expected '" << expected
              << "')\n";
    ++failures;
  }
}

void expect_eq_size(std::size_t actual, std::size_t expected, const char* message) {
  if (actual != expected) {
    std::cerr << "FAIL: " << message << " (got " << actual << ", expected " << expected << ")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const fs::path base = fs::temp_directory_path() / "tuide_recent_projects_test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base / "home", ec);
  setenv("HOME", (base / "home").c_str(), 1);

  for (int i = 1; i <= 6; ++i) {
    fs::create_directories(base / ("proj" + std::to_string(i)), ec);
  }

  {
    tuide::RecentProjects recent;
    expect_true(recent.remember((base / "proj1").string()), "remember proj1");
    expect_true(recent.remember((base / "proj2").string()), "remember proj2");
    expect_true(recent.remember((base / "proj3").string()), "remember proj3");
    expect_eq_size(recent.paths.size(), 3, "three projects stored");
    expect_eq(recent.paths[0], fs::weakly_canonical(base / "proj3", ec).string(),
              "newest first");
  }

  {
    tuide::RecentProjects loaded = tuide::RecentProjects::load();
    expect_eq_size(loaded.paths.size(), 3, "load persists three projects");
    expect_eq(loaded.paths[0], fs::weakly_canonical(base / "proj3", ec).string(),
              "loaded newest first");
  }

  {
    tuide::RecentProjects recent = tuide::RecentProjects::load();
    expect_true(recent.remember((base / "proj1").string()), "re-remember proj1 moves to front");
    expect_eq(recent.paths[0], fs::weakly_canonical(base / "proj1", ec).string(),
              "reopened project is first");
    expect_eq_size(recent.paths.size(), 3, "dedupe keeps size");
  }

  {
    tuide::RecentProjects recent = tuide::RecentProjects::load();
    for (int i = 1; i <= 6; ++i) {
      expect_true(recent.remember((base / ("proj" + std::to_string(i))).string()),
                  "remember many projects");
    }
    expect_eq_size(recent.paths.size(), tuide::RecentProjects::kMaxRecent, "caps at 5");
    expect_eq(recent.paths[0], fs::weakly_canonical(base / "proj6", ec).string(),
              "most recent is proj6");
  }

  {
    tuide::RecentProjects recent = tuide::RecentProjects::load();
    fs::remove_all(base / "proj6", ec);
    const auto existing = recent.existing_paths();
    expect_eq_size(existing.size(), 4, "filters missing directories");
    for (const auto& path : existing) {
      expect_true(path.find("proj6") == std::string::npos, "missing project filtered");
    }
  }

  fs::remove_all(base, ec);
  if (failures > 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "OK\n";
  return 0;
}
