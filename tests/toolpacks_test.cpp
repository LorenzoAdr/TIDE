#include "toolpacks/catalog.hpp"
#include "toolpacks/download.hpp"
#include "toolpacks/export_portable.hpp"
#include "toolpacks/install.hpp"
#include "toolpacks/language_packs.hpp"
#include "toolpacks/manifest.hpp"
#include "toolpacks/packaged.hpp"
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

fs::path make_temp_root(const char* name) {
  const auto base = fs::temp_directory_path() / name;
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

int run_shell(const std::string& cmd) {
  return std::system(cmd.c_str());
}

void test_manifest_roundtrip(const fs::path& root) {
  std::error_code ec;
  fs::create_directories(root, ec);
  expect(!ec, "create manifest root");

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

  setenv("CLANGD_PATH", "/bin/true", 1);
  const auto env_loc = tuide::resolve_clangd();
  expect(env_loc.has_value(), "env resolve");
  expect(env_loc->source == tuide::ClangdLocation::Source::Env, "source env");
  unsetenv("CLANGD_PATH");
}

void test_install_from_local_catalog(const fs::path& root) {
  const fs::path fixture = root / "fixture";
  const fs::path store = root / "store";
  const fs::path payload = fixture / "payload";
  std::error_code ec;
  fs::create_directories(store, ec);
  fs::create_directories(payload, ec);

  write_file(payload / "bin" / "clangd", "#!/bin/sh\necho toolpack-clangd\n");
  fs::permissions(payload / "bin" / "clangd",
                  fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  write_file(payload / "lib" / "clang" / "19" / "include" / ".keep", "");
  write_file(payload / "toolpack.json", R"({
  "schema": 1,
  "id": "clangd",
  "version": "19.1.2",
  "entry": { "type": "executable", "path": "bin/clangd" }
})");

  const fs::path archive = fixture / "clangd-19.1.2-linux-x86_64.tar.zst";
  const std::string tar_cmd =
      "tar -C " + payload.string() + " -cf - . | zstd -q -o " + archive.string();
  expect(run_shell(tar_cmd.c_str()) == 0, "create tar.zst");

  const std::string sha = tuide::toolpacks::file_sha256(archive.string());
  expect(sha.size() == 64, "sha256");

  const fs::path catalog_path = fixture / "catalog.json";
  write_file(catalog_path,
             std::string("{\n") +
                 "  \"schema\": 1,\n"
                 "  \"toolpacks\": [{\n"
                 "    \"id\": \"clangd\",\n"
                 "    \"version\": \"19.1.2\",\n"
                 "    \"arch\": [\"x86_64\"],\n"
                 "    \"os\": [\"linux\"],\n"
                 "    \"url\": \"file://" +
                 archive.string() +
                 "\",\n"
                 "    \"sha256\": \"" +
                 sha +
                 "\"\n"
                 "  }]\n"
                 "}\n");

  setenv("TUIDE_TOOLPACKS_ROOT", store.string().c_str(), 1);
  setenv("TUIDE_TOOLPACKS_CATALOG_URL", catalog_path.string().c_str(), 1);
  unsetenv("CLANGD_PATH");

  const auto installed = tuide::toolpacks::install_toolpack("clangd");
  expect(installed.ok, installed.message.c_str());
  expect(installed.version == "19.1.2", "installed version");

  const auto loc = tuide::resolve_clangd();
  expect(loc.has_value(), "resolve after install");
  expect(loc->source == tuide::ClangdLocation::Source::Toolpack, "toolpack source");

  const auto removed = tuide::toolpacks::remove_toolpack("clangd");
  expect(removed.ok, removed.message.c_str());
  expect(!tuide::toolpacks::resolve_clangd_toolpack().has_value(), "gone after remove");
}

void test_export_appdir(const fs::path& root) {
  const fs::path fixture = root / "fixture";
  const fs::path store = root / "store";
  const fs::path payload = fixture / "payload";
  std::error_code ec;
  fs::create_directories(store, ec);

  write_file(payload / "bin" / "clangd", "#!/bin/sh\necho appdir-clangd\n");
  fs::permissions(payload / "bin" / "clangd",
                  fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  write_file(payload / "lib" / "clang" / "19" / "include" / ".keep", "");
  write_file(payload / "toolpack.json", R"({
  "schema": 1,
  "id": "clangd",
  "version": "19.1.2",
  "entry": { "type": "executable", "path": "bin/clangd" }
})");
  const fs::path archive = fixture / "clangd-19.1.2-linux-x86_64.tar.zst";
  expect(run_shell(("tar -C " + payload.string() + " -cf - . | zstd -q -o " + archive.string())
                       .c_str()) == 0,
         "archive");
  const std::string sha = tuide::toolpacks::file_sha256(archive.string());
  const fs::path catalog_path = fixture / "catalog.json";
  write_file(catalog_path,
             std::string("{\"schema\":1,\"toolpacks\":[{\"id\":\"clangd\",\"version\":\"19.1.2\",") +
                 "\"arch\":[\"x86_64\"],\"os\":[\"linux\"],\"url\":\"file://" + archive.string() +
                 "\",\"sha256\":\"" + sha + "\"}]}");

  setenv("TUIDE_TOOLPACKS_ROOT", store.string().c_str(), 1);
  setenv("TUIDE_TOOLPACKS_CATALOG_URL", catalog_path.string().c_str(), 1);
  expect(tuide::toolpacks::install_toolpack("clangd").ok, "install for export");

  const fs::path fake_core = root / "fake-core";
  write_file(fake_core, std::string("FAKECORE") + std::string(64, 'x'));
  fs::permissions(fake_core, fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);

  const fs::path out = root / "tuide.AppDir";
  const auto exported = tuide::toolpacks::export_portable(
      fake_core.string(), out.string(), {"clangd"}, tuide::toolpacks::ExportFormat::kAppDir);
  expect(exported.ok, exported.message.c_str());
  expect(tuide::toolpacks::path_looks_like_appdir(out.string()), "is appdir");
  expect(fs::is_regular_file(out / "AppRun"), "AppRun");
  expect(fs::is_regular_file(out / "usr" / "bin" / "tuide"), "tuide binary");
  expect(fs::is_regular_file(out / "usr" / "share" / "tuide" / "toolpacks" / "clangd" / "19.1.2" /
                             "bin" / "clangd"),
         "clangd in appdir");
  expect(fs::is_regular_file(out / "usr" / "share" / "tuide" / "toolpacks" / "manifest.json"),
         "manifest");

  // Resolve via TUIDE_TOOLPACKS_ROOT pointing at AppDir toolpacks.
  setenv("TUIDE_TOOLPACKS_ROOT",
         (out / "usr" / "share" / "tuide" / "toolpacks").string().c_str(), 1);
  unsetenv("CLANGD_PATH");
  const auto loc = tuide::resolve_clangd();
  expect(loc.has_value(), "resolve from appdir toolpacks");
  expect(loc->source == tuide::ClangdLocation::Source::Toolpack, "toolpack source");

  const auto blocked = tuide::toolpacks::export_portable(
      out.string(), (root / "blocked.AppDir").string(), {"clangd"},
      tuide::toolpacks::ExportFormat::kAppDir);
  expect(!blocked.ok, "re-export blocked");
  expect(blocked.message.find("empaquetado") != std::string::npos ||
             blocked.message.find("nucleo limpio") != std::string::npos,
         "blocked message");
}

void test_language_pack_cpp_status(const fs::path& root) {
  setenv("TUIDE_TOOLPACKS_ROOT", root.string().c_str(), 1);
  const auto* pack = tuide::toolpacks::find_language_pack("cpp");
  expect(pack != nullptr, "cpp pack exists");
  auto status = tuide::toolpacks::language_pack_status(*pack);
  expect(status.status == tuide::toolpacks::LanguagePackStatus::kMissing, "missing initially");
  expect(status.missing_ids.size() == 2, "clangd+gdb missing");

  const fs::path clangd = root / "clangd" / "19.1.2";
  write_file(clangd / "bin" / "clangd", "#!/bin/sh\n");
  fs::permissions(clangd / "bin" / "clangd",
                  fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  write_file(clangd / "toolpack.json",
             R"({"schema":1,"id":"clangd","version":"19.1.2","entry":{"type":"executable","path":"bin/clangd"}})");
  tuide::toolpacks::Manifest manifest;
  tuide::toolpacks::ManifestEntry entry;
  entry.id = "clangd";
  entry.version = "19.1.2";
  entry.active = true;
  entry.path = "clangd/19.1.2";
  manifest.installed.push_back(entry);
  expect(tuide::toolpacks::save_manifest((root / "manifest.json").string(), manifest), "manifest");

  status = tuide::toolpacks::language_pack_status(*pack);
  expect(status.status == tuide::toolpacks::LanguagePackStatus::kPartial, "partial");
}

}  // namespace

int main() {
  const fs::path root = make_temp_root("tuide-toolpacks-test");
  test_manifest_roundtrip(root / "manifest");
  test_resolve_clangd_toolpack(root / "resolve");
  test_install_from_local_catalog(root / "install");
  test_export_appdir(root / "export");
  test_language_pack_cpp_status(root / "langpack");
  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << "toolpacks_test: OK\n";
  return 0;
}
