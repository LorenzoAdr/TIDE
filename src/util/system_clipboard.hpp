#pragma once

#include <string>

namespace tuide {

// Eagerly dlopen libX11 (if DISPLAY is set) and cache CLI clipboard backends.
void warm_system_clipboard();

bool set_system_clipboard(const std::string& text);
std::string get_system_clipboard();

}  // namespace tuide
