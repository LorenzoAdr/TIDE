#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ftxui/dom/elements.hpp"

namespace tuide {

struct AiResultLocation {
  std::string path;
  int line = 0;  // 1-based; 0 = unknown / open at top
};

// Numbered investigate rows: "1. path/to/file.cpp:42"
std::optional<AiResultLocation> parse_ai_result_location(std::string_view line);

// Render one AI console transcript row: CodeBg-friendly styling, reply
// indentation, and lightweight syntax color for C++-like method snippets.
// When `hovered`, numbered result rows get list-row hover affordance.
ftxui::Element render_ai_transcript_line(std::string_view line, bool hovered = false);

}  // namespace tuide
