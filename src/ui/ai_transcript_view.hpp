#pragma once

#include <cstddef>
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

// Visible text after stripping closed markdown markers (**…**, __…__, `…`).
// Unmatched markers are kept. Used by tests and wrap-safe rendering.
std::string ai_markdown_plain(std::string_view line);

// Render one AI console transcript row. Visual hierarchy:
// pilot narrative (`→ Piloto:` / `→ Explorador:` / …) accent+bold;
// explorer detail (`  · …`) muted; reply body TitleText + light markdown;
// syntax color only for clear C++-like investigate signatures.
// When `hovered`, numbered result rows get list-row hover affordance.
//
// `slice_begin`/`slice_end` are byte offsets into `line` for a soft-wrapped
// segment. Markdown is parsed on the FULL line so `**` split across wraps
// still disappears (markers are gaps, never painted).
ftxui::Element render_ai_transcript_line(std::string_view line, bool hovered = false,
                                         std::size_t slice_begin = 0,
                                         std::size_t slice_end = std::string_view::npos);

}  // namespace tuide
