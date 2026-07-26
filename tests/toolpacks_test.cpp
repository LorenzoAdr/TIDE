#include "toolpacks/manifest.hpp"
#include "toolpacks/paths.hpp"
#include "toolpacks/store.hpp"
#include "util/bundled_tools.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

fs::path make_temp_root() {
  const auto base = fs::temp_directory_path() / "tuide-toolpacks-test";
  std::error_code ec;
  fs::remove_all(base, ec);
  fs::create_directories(base, ec);
  expect(!ec, "create temp root");
  return base;
}

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::trunc);
  expect(static_cast<bool>(out), "write file");
  out << text;
}

void test_manifest_roundtrip(const fs::path& root) {
  tuide::toolpacks::Manifest manifest;
  manifest.schema = 1;
  tuide::toolpacks::ManifestEntry entry;
  entry.id = "clangd";
  entry.version = "19.1.2";
  entry.active = true;
  entry.source = "catalog";
  entry.path = "clangd/19.1.2";
  entry.installed_at = "2026-07-26T20:00:00Z";
  manifest.installed.push_back(entry);

  const fs::path path = root / "manifest.json";
  expect(tuide::toolpacks::save_manifest(path.string(), manifest), "save manifest");
  const auto loaded = tuide::toolpacks::load_manifest(path.string());
  expect(loaded.has_value(), "load manifest");
  expect(loaded->installed.size() == 1, "one entry");
  expect(loaded->installed[0].id == "clangd", "id");
  expect(loaded->installed[0].active, "active");
  const auto active = tuide::toolpacks::find_active_entry(*loaded, "clangd");
  expect(active.has_value(), "find active");
  expect(active->version == "19.1.2", "version");
}

void test_resolve_clangd_toolpack(const fs::path& root) {
  setenv("TUIDE_TOOLPACKS_ROOT", root.string().c_str(), 1);
  unsetenv("CLANGD_PATH");

  const fs::path pack = root / "clangd" / "19.1.2";
  const fs::path bin = pack / "bin" / "clangd";
  write_file(bin, "#!/bin/sh\necho fake-clangd\n");
  fs::permissions(bin, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                            fs::perms::others_read | fs::perms::others_exec);

  write_file(pack / "lib" / "clang" / "19" / "include" / ".keep", "");
  write_file(pack / "toolpack.json", R"({
  "schema": 1,
  "id": "clangd",
  "version": "19.1.2",
  "entry": { "type": "executable", "path": "bin/clangd" }
})");

  tuide::toolpacks::Manifest manifest;
  tuide::toolpacks::ManifestEntry entry;
  entry.id = "clangd";
  entry.version = "19.1.2";
  entry.active = true;
  entry.path = "clangd/19.1.2";
  entry.source = "test";
  manifest.installed.push_back(entry);
  expect(tuide::toolpacks::save_manifest((root / "manifest.json").string(), manifest),
         "save for resolve");

  const auto tp = tuide::toolpacks::resolve_clangd_toolpack();
  expect(tp.has_value(), "resolve toolpack");
  expect(tp->binary_path == bin.string(), "binary path");
  expect(!tp->resource_dir.empty(), "resource dir detected");

  const auto loc = tuide::resolve_clangd();
  expect(loc.has_value(), "resolve_clangd");
  expect(loc->source == tuide::ClangdLocation::Source::Toolpack, "source toolpack");
  expect(loc->binary_path == bin.string(), "resolve path");

  // Env still wins over toolpack.
  setenv("CLANGD_PATH", "/bin/true", 1);
  const auto env_loc = tuide::resolve_clangd();
  expect(env_loc.has_value(), "env resolve");
  expect(env_loc->source == tuide::ClangdLocation::Source::Env, "source env");
  unsetenv("CLANGD_PATH");
}

}  // namespace

int main() {
  const fs::path root = make_temp_root();
  test_manifest_roundtrip(root);
  test_resolve_clangd_toolpack(root);
  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << "toolpacks_test: OK\n";
  return 0;
}
