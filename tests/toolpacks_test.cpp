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
#include <iterator>
#include <string>
#include <utility>

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

std::string host_arch() { return tuide::toolpacks::host_catalog_arch(); }

std::string linux_infix() { return tuide::toolpacks::linux_catalog_archive_infix(); }

void test_catalog_selects_host_arch() {
  const std::string host = host_arch();
  const std::string other = (host == "aarch64") ? "x86_64" : "aarch64";
  const std::string json = std::string("{\n") +
      "  \"schema\": 1,\n"
      "  \"toolpacks\": [\n"
      "    {\"id\":\"clangd\",\"version\":\"19.1.2\",\"arch\":[\"" + other +
      "\"],\"os\":[\"linux\"],"
      "\"url\":\"file:///tmp/clangd-" + other +
      ".tar.zst\",\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},\n"
      "    {\"id\":\"clangd\",\"version\":\"19.1.2\",\"arch\":[\"" + host +
      "\"],\"os\":[\"linux\"],"
      "\"url\":\"file:///tmp/clangd-" + host +
      ".tar.zst\",\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}\n"
      "  ]\n"
      "}\n";
  const auto catalog = tuide::toolpacks::parse_catalog_json(json);
  expect(catalog.has_value(), "parse dual-arch catalog");
  const auto found = tuide::toolpacks::find_catalog_toolpack(*catalog, "clangd");
  expect(found.has_value(), "find clangd for host");
  expect(found->sha256.find("bbbbbbbb") == 0, "picked host-arch sha");
  expect(found->url.find(host) != std::string::npos, "picked host-arch url");

  const auto other_only = tuide::toolpacks::parse_catalog_json(
      std::string("{\"schema\":1,\"toolpacks\":[{\"id\":\"clangd\",\"version\":\"1\",") +
      "\"arch\":[\"" + other +
      "\"],\"url\":\"file:///x\",\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}");
  expect(other_only.has_value(), "parse other-arch-only catalog");
  expect(!tuide::toolpacks::find_catalog_toolpack(*other_only, "clangd").has_value(),
         "other-arch only is not selected");
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

  const fs::path archive = fixture / ("clangd-19.1.2-" + linux_infix() + ".tar.zst");
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
                 "    \"arch\": [\"" + host_arch() + "\"],\n"
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
  const fs::path archive = fixture / ("clangd-19.1.2-" + linux_infix() + ".tar.zst");
  expect(run_shell(("tar -C " + payload.string() + " -cf - . | zstd -q -o " + archive.string())
                       .c_str()) == 0,
         "archive");
  const std::string sha = tuide::toolpacks::file_sha256(archive.string());
  const fs::path catalog_path = fixture / "catalog.json";
  write_file(catalog_path,
             std::string("{\"schema\":1,\"toolpacks\":[{\"id\":\"clangd\",\"version\":\"19.1.2\",") +
                 "\"arch\":[\"" + host_arch() + "\"],\"os\":[\"linux\"],\"url\":\"file://" + archive.string() +
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
  {
    std::ifstream apprun(out / "AppRun");
    std::string apprun_text((std::istreambuf_iterator<char>(apprun)),
                            std::istreambuf_iterator<char>());
    expect(apprun_text.find("TUIDE_TOOLPACKS_BUNDLED=") != std::string::npos,
           "AppRun exports bundled root");
    expect(apprun_text.find("TUIDE_TOOLPACKS_ROOT=") == std::string::npos,
           "AppRun does not force writable root onto squashfs");
  }
  expect(fs::is_regular_file(out / "usr" / "bin" / "tuide"), "tuide binary");
  expect(fs::is_regular_file(out / "usr" / "share" / "tuide" / "toolpacks" / "clangd" / "19.1.2" /
                             "bin" / "clangd"),
         "clangd in appdir");
  expect(fs::is_regular_file(out / "usr" / "share" / "tuide" / "toolpacks" / "manifest.json"),
         "manifest");

  // Non-pilot ids must export too (whitelist removed).
  write_file(payload / "bin" / "make-ls", "#!/bin/sh\necho make-ls\n");
  // reuse store: install make-ls via local catalog entry
  write_file(store / "make-ls" / "v0.1.16" / "bin" / "make-ls", "#!/bin/sh\necho make-ls\n");
  fs::permissions(store / "make-ls" / "v0.1.16" / "bin" / "make-ls",
                  fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  write_file(store / "make-ls" / "v0.1.16" / "toolpack.json",
             R"({"schema":1,"id":"make-ls","version":"v0.1.16","entry":{"type":"executable","path":"bin/make-ls"}})");
  {
    auto manifest = tuide::toolpacks::load_manifest((store / "manifest.json").string());
    expect(manifest.has_value(), "manifest after clangd");
    tuide::toolpacks::ManifestEntry mk;
    mk.id = "make-ls";
    mk.version = "v0.1.16";
    mk.active = true;
    mk.path = "make-ls/v0.1.16";
    mk.source = "test";
    manifest->installed.push_back(mk);
    expect(tuide::toolpacks::save_manifest((store / "manifest.json").string(), *manifest),
           "save make-ls");
  }
  const fs::path out2 = root / "tuide-all.AppDir";
  const auto exported2 = tuide::toolpacks::export_portable(
      fake_core.string(), out2.string(), {"clangd", "make-ls"},
      tuide::toolpacks::ExportFormat::kAppDir);
  expect(exported2.ok, exported2.message.c_str());
  expect(fs::is_regular_file(out2 / "usr" / "share" / "tuide" / "toolpacks" / "make-ls" /
                             "v0.1.16" / "bin" / "make-ls"),
         "make-ls in appdir");

  // Resolve via bundled AppImage packs while user root stays empty/writable.
  unsetenv("TUIDE_TOOLPACKS_ROOT");
  setenv("TUIDE_TOOLPACKS_BUNDLED",
         (out / "usr" / "share" / "tuide" / "toolpacks").string().c_str(), 1);
  setenv("XDG_DATA_HOME", (root / "xdg-data").string().c_str(), 1);
  unsetenv("CLANGD_PATH");
  const auto loc = tuide::resolve_clangd();
  expect(loc.has_value(), "resolve from bundled toolpacks");
  expect(loc->source == tuide::ClangdLocation::Source::Toolpack, "toolpack source");

  // Catalog install must land in the writable XDG root, not the bundled tree.
  const auto installed_again = tuide::toolpacks::install_toolpack("clangd");
  expect(installed_again.ok, installed_again.message.c_str());
  expect(installed_again.root_path.find((root / "xdg-data").string()) != std::string::npos,
         "install writes to XDG, not bundled");

  const auto blocked = tuide::toolpacks::export_portable(
      out.string(), (root / "blocked.AppDir").string(), {"clangd"},
      tuide::toolpacks::ExportFormat::kAppDir);
  expect(!blocked.ok, "re-export blocked");
  expect(blocked.message.find("empaquetado") != std::string::npos ||
             blocked.message.find("nucleo limpio") != std::string::npos,
         "blocked message");

  unsetenv("TUIDE_TOOLPACKS_BUNDLED");

  // Core-only AppDir (no toolpacks) for official slim releases.
  const fs::path out_core = root / "tuide-core.AppDir";
  const auto core_only = tuide::toolpacks::export_portable(
      fake_core.string(), out_core.string(), {}, tuide::toolpacks::ExportFormat::kAppDir, {},
      true);
  expect(core_only.ok, core_only.message.c_str());
  expect(tuide::toolpacks::path_looks_like_appdir(out_core.string()), "core appdir");
  expect(fs::is_regular_file(out_core / "usr" / "bin" / "tuide"), "core binary");
  expect(!fs::exists(out_core / "usr" / "share" / "tuide" / "toolpacks" / "clangd"),
         "no clangd in core-only");
  expect(fs::is_regular_file(out_core / "usr" / "share" / "tuide" / "toolpacks" / "manifest.json"),
         "empty manifest present");
  {
    std::ifstream apprun(out_core / "AppRun");
    std::string apprun_text((std::istreambuf_iterator<char>(apprun)),
                            std::istreambuf_iterator<char>());
    expect(apprun_text.find("TUIDE_TOOLPACKS_BUNDLED=") != std::string::npos,
           "core AppRun still exports bundled path");
  }
}

void test_readonly_root_falls_back_to_xdg(const fs::path& root) {
  const fs::path ro = root / "ro-store";
  const fs::path xdg = root / "xdg-data";
  std::error_code ec;
  fs::create_directories(ro, ec);
  write_file(ro / "manifest.json", R"({"schema":1,"installed":[]})");
  fs::permissions(ro, fs::perms::owner_read | fs::perms::owner_exec | fs::perms::group_read |
                           fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
  setenv("TUIDE_TOOLPACKS_ROOT", ro.string().c_str(), 1);
  unsetenv("TUIDE_TOOLPACKS_BUNDLED");
  setenv("XDG_DATA_HOME", xdg.string().c_str(), 1);

  expect(tuide::toolpacks::toolpacks_root().find(xdg.string()) != std::string::npos,
         "writable root falls back to XDG");
  expect(tuide::toolpacks::bundled_toolpacks_root() == ro.string(),
         "RO ROOT is treated as bundled");
  expect(tuide::toolpacks::toolpacks_root_is_writable(), "xdg root writable");

  // Restore perms so temp cleanup works.
  fs::permissions(ro, fs::perms::owner_all, ec);
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

void test_install_make_ls_and_rust_pack(const fs::path& root) {
  const fs::path fixture = root / "fixture";
  const fs::path store = root / "store";
  std::error_code ec;
  fs::create_directories(store, ec);

  auto make_pack = [&](const std::string& id, const std::string& version,
                       const std::string& bin_name) {
    const fs::path payload = fixture / (id + "-payload");
    write_file(payload / "bin" / bin_name, "#!/bin/sh\necho " + id + "\n");
    fs::permissions(payload / "bin" / bin_name,
                    fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
    write_file(payload / "toolpack.json",
               std::string("{\"schema\":1,\"id\":\"") + id + "\",\"version\":\"" + version +
                   "\",\"entry\":{\"type\":\"executable\",\"path\":\"bin/" + bin_name + "\"}}");
    const fs::path archive = fixture / (id + "-" + version + "-" + linux_infix() + ".tar.zst");
    expect(run_shell(("tar -C " + payload.string() + " -cf - . | zstd -q -o " + archive.string())
                         .c_str()) == 0,
           "archive");
    return std::make_pair(archive, tuide::toolpacks::file_sha256(archive.string()));
  };

  const auto make_ls = make_pack("make-ls", "v0.1.16", "make-ls");
  const auto rust = make_pack("rust-analyzer", "2025-12-29", "rust-analyzer");
  // Fake --version responder for rust-analyzer accept().
  write_file(fixture / "rust-analyzer-payload" / "bin" / "rust-analyzer",
             "#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then echo rust-analyzer 1.0; exit 0; fi\n"
             "echo ok\n");
  fs::permissions(fixture / "rust-analyzer-payload" / "bin" / "rust-analyzer",
                  fs::perms::owner_all | fs::perms::group_exec | fs::perms::others_exec);
  expect(run_shell(("tar -C " + (fixture / "rust-analyzer-payload").string() +
                    " -cf - . | zstd -f -q -o " + rust.first.string())
                       .c_str()) == 0,
         "rearchive rust");
  const std::string rust_sha = tuide::toolpacks::file_sha256(rust.first.string());

  const auto gdb = make_pack("gdb", "16.3-static", "gdb");

  const fs::path catalog_path = fixture / "catalog.json";
  write_file(catalog_path,
             std::string("{\"schema\":1,\"toolpacks\":[") +
                 "{\"id\":\"make-ls\",\"version\":\"v0.1.16\",\"arch\":[\"" + host_arch() +
                 "\"],\"os\":[\"linux\"],"
                 "\"url\":\"file://" +
                 make_ls.first.string() + "\",\"sha256\":\"" + make_ls.second + "\"}," +
                 "{\"id\":\"rust-analyzer\",\"version\":\"2025-12-29\",\"arch\":[\"" + host_arch() +
                 "\"],"
                 "\"os\":[\"linux\"],\"url\":\"file://" +
                 rust.first.string() + "\",\"sha256\":\"" + rust_sha + "\"}," +
                 "{\"id\":\"gdb\",\"version\":\"16.3-static\",\"arch\":[\"" + host_arch() +
                 "\"],\"os\":[\"linux\"],"
                 "\"url\":\"file://" +
                 gdb.first.string() + "\",\"sha256\":\"" + gdb.second + "\"}]}");

  setenv("TUIDE_TOOLPACKS_ROOT", store.string().c_str(), 1);
  setenv("TUIDE_TOOLPACKS_CATALOG_URL", catalog_path.string().c_str(), 1);
  unsetenv("TUIDE_MAKE_LS");
  unsetenv("TUIDE_RUST_ANALYZER");
  unsetenv("GDB_PATH");

  expect(tuide::toolpacks::find_language_pack("rust") != nullptr, "rust language pack");
  expect(tuide::toolpacks::find_language_pack("make") != nullptr, "make language pack");

  const auto make_install = tuide::toolpacks::install_toolpack("make-ls");
  expect(make_install.ok, make_install.message.c_str());
  const auto make_loc = tuide::resolve_make_ls();
  expect(make_loc.has_value(), "resolve make-ls");
  expect(make_loc->source == tuide::MakeLsLocation::Source::Toolpack, "make-ls toolpack source");

  const auto rust_pack = tuide::toolpacks::install_language_pack("rust");
  expect(rust_pack.ok, rust_pack.message.c_str());
  const auto ra = tuide::resolve_rust_analyzer();
  expect(ra.has_value(), "resolve rust-analyzer");
  expect(ra->source == tuide::RustAnalyzerLocation::Source::Toolpack, "ra toolpack");
  const auto gdb_loc = tuide::resolve_gdb();
  expect(gdb_loc.has_value(), "resolve gdb from rust pack");
  expect(gdb_loc->source == tuide::GdbLocation::Source::Toolpack, "gdb toolpack");
}

}  // namespace

int main() {
  const fs::path root = make_temp_root("tuide-toolpacks-test");
  test_catalog_selects_host_arch();
  test_manifest_roundtrip(root / "manifest");
  test_resolve_clangd_toolpack(root / "resolve");
  test_install_from_local_catalog(root / "install");
  test_export_appdir(root / "export");
  test_readonly_root_falls_back_to_xdg(root / "ro-fallback");
  test_language_pack_cpp_status(root / "langpack");
  test_install_make_ls_and_rust_pack(root / "extra");
  std::error_code ec;
  fs::remove_all(root, ec);
  std::cout << "toolpacks_test: OK\n";
  return 0;
}
