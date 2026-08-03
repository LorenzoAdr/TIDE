#include "ui/keybind/bindings_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace tuide {

std::string key_bindings_config_path() {
  const char* home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') {
    return {};
  }
  return (fs::path(home) / ".config/tuide/bindings.json").string();
}

bool load_key_bindings_file(KeyBindingRegistry* registry, std::string* error) {
  if (registry == nullptr) {
    if (error != nullptr) {
      *error = "null registry";
    }
    return false;
  }
  const std::string path = key_bindings_config_path();
  if (path.empty()) {
    return true;
  }
  std::ifstream input(path);
  if (!input) {
    return true;  // missing file is OK
  }
  try {
    nlohmann::json doc;
    input >> doc;
    return registry->import_overrides(doc, error);
  } catch (const std::exception& ex) {
    if (error != nullptr) {
      *error = ex.what();
    }
    return false;
  }
}

bool save_key_bindings_file(const KeyBindingRegistry& registry, std::string* error) {
  const std::string path = key_bindings_config_path();
  if (path.empty()) {
    if (error != nullptr) {
      *error = "HOME not set";
    }
    return false;
  }
  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  std::ofstream output(path);
  if (!output) {
    if (error != nullptr) {
      *error = "cannot write " + path;
    }
    return false;
  }
  output << registry.export_overrides().dump(2) << '\n';
  if (!output) {
    if (error != nullptr) {
      *error = "write failed: " + path;
    }
    return false;
  }
  return true;
}

}  // namespace tuide
