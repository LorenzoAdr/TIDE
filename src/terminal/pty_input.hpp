#pragma once

#include <optional>
#include <string>

#include "ftxui/component/event.hpp"

namespace tuide {

std::optional<std::string> event_to_pty_bytes(const ftxui::Event& event);

}  // namespace tuide
