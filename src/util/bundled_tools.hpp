#pragma once

#include <optional>
#include <string>

namespace tgdb {

struct ClangdLocation {
  std::string binary_path;
  std::string resource_dir;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

struct GdbLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

struct RgLocation {
  std::string binary_path;
  enum class Source { Env, SystemPath, Bundled } source = Source::Bundled;
};

bool has_bundled_clangd();
bool has_bundled_gdb();
bool has_bundled_rg();

void set_runtime_force_bundled_clangd(bool value);
void set_runtime_force_bundled_gdb(bool value);
void set_runtime_force_bundled_rg(bool value);

bool should_force_bundled_clangd();
bool should_force_bundled_gdb();
bool should_force_bundled_rg();

std::optional<ClangdLocation> resolve_clangd();
std::optional<GdbLocation> resolve_gdb();
std::optional<RgLocation> resolve_rg();

}  // namespace tgdb
