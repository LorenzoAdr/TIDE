#pragma once

#include <optional>
#include <string>

namespace tuide::toolpacks {

struct ToolpackEntry {
  std::string type;  // executable | node_script | python_module
  std::string path;  // relative to toolpack root
  // Optional explicit resource dir (clangd); empty = auto-detect.
  std::string resource_dir;
};

struct ToolpackMeta {
  int schema = 1;
  std::string id;
  std::string version;
  ToolpackEntry entry;
  std::string license;
};

struct ResolvedToolpack {
  std::string id;
  std::string version;
  std::string root_dir;
  std::string binary_path;
  std::string resource_dir;
};

std::optional<ToolpackMeta> load_toolpack_meta(const std::string& toolpack_json_path);

// Resolve active toolpack by id from the local store.
std::optional<ResolvedToolpack> resolve_installed_toolpack(const std::string& id);

// Clangd helper: binary + resource-dir under an installed toolpack root.
std::optional<ResolvedToolpack> resolve_clangd_toolpack();

bool is_executable_path(const std::string& path);

}  // namespace tuide::toolpacks
