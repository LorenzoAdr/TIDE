#pragma once

#include <string>

namespace tuide::toolpacks {

struct InstallResult {
  bool ok = false;
  std::string message;
  std::string id;
  std::string version;
  std::string root_path;
};

// id may be "clangd" or "clangd@19.1.2".
InstallResult install_toolpack(const std::string& id_spec);
InstallResult update_toolpack(const std::string& id);
InstallResult remove_toolpack(const std::string& id);

}  // namespace tuide::toolpacks
