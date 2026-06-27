#pragma once

#include <string>

#include "ftxui/dom/elements.hpp"

namespace tgdb {

ftxui::Element HighlightCppLine(const std::string& line, int cursor_col = -1,
                                ftxui::Decorator cursor_style = {});

}  // namespace tgdb
