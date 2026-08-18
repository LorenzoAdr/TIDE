#pragma once

#include <string>

#include "symbols/symbol_kind.hpp"

namespace tuide {

enum class IconMode { Auto, Always, Never };

void configure_glyphs(IconMode mode);
bool glyphs_use_nerd();

std::string symbol_kind_glyph(SymbolKind kind);
std::string file_glyph(const std::string& filename);
std::string file_glyph_display(const std::string& filename);
std::string folder_glyph(bool expanded);
std::string twistie_glyph(bool expanded);
std::string strip_symbol_kind_prefix(const std::string& prefixed_name);

IconMode parse_icon_mode_env();
IconMode resolve_icon_mode(IconMode settings_mode);

}  // namespace tuide
