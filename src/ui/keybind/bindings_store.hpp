#pragma once

#include <string>

#include "ui/keybind/key_binding_registry.hpp"

namespace tuide {

std::string key_bindings_config_path();
bool load_key_bindings_file(KeyBindingRegistry* registry, std::string* error = nullptr);
bool save_key_bindings_file(const KeyBindingRegistry& registry, std::string* error = nullptr);

}  // namespace tuide
