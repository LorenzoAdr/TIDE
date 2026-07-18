#pragma once

#include <string>

namespace tuide {

bool set_system_clipboard(const std::string& text);
std::string get_system_clipboard();

}  // namespace tuide
