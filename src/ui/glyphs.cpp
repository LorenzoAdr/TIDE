#include "ui/glyphs.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

namespace tgdb {

namespace {

constexpr const char* kSymbolKindPrefixes[] = {"ns ", "C ", "S ", "M ", "v ", "f "};

struct GlyphPair {
  const char* nerd;
  const char* ascii;
};

constexpr GlyphPair kSymbolGlyphs[] = {
    {u8"\uEA8F", "ns"},  // namespace
    {u8"\uEA8B", "C"},   // class
    {u8"\uEA8D", "S"},   // struct
    {u8"\uEA8C", "M"},   // method
    {u8"\uEA88", "v"},   // variable
    {u8"\uEA8A", "f"},   // function
};

constexpr const char* kFileCpp = u8"\uE61D";
constexpr const char* kFileHeader = u8"\uE60A";
constexpr const char* kFileCmake = u8"\uE794";
constexpr const char* kFileMakefile = u8"\uE712";
constexpr const char* kFileJson = u8"\uE60B";
constexpr const char* kFileMarkdown = u8"\uE73E";
constexpr const char* kFileDefault = u8"\uE612";
constexpr const char* kFolderClosed = u8"\uE5FF";
constexpr const char* kFolderOpen = u8"\uE5FC";

IconMode configured_mode_ = IconMode::Auto;
bool use_nerd_ = false;
bool configured_ = false;

std::string to_lower(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool locale_is_utf8() {
  auto check = [](const char* name) {
    if (name == nullptr || name[0] == '\0') {
      return false;
    }
    std::string value(name);
    const std::string lower = to_lower(value);
    return lower.find("utf-8") != std::string::npos || lower.find("utf8") != std::string::npos;
  };
  return check(std::getenv("LC_ALL")) || check(std::getenv("LC_CTYPE")) ||
         check(std::getenv("LANG"));
}

bool env_truthy(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const std::string lower = to_lower(value);
  return lower == "1" || lower == "yes" || lower == "true" || lower == "on";
}

bool nerd_font_hint_env() {
  return env_truthy("NERD_FONT") || env_truthy("TGDB_NERD_FONT");
}

bool terminal_likely_has_nerd_font() {
  const char* term = std::getenv("TERM");
  if (term != nullptr && term[0] != '\0') {
    const std::string term_lower = to_lower(term);
    static const char* const kLikelyTerms[] = {"kitty",   "wezterm", "alacritty", "foot",
                                               "ghostty", "iterm",   "tabby",     "rio"};
    for (const char* hint : kLikelyTerms) {
      if (term_lower.find(hint) != std::string::npos) {
        return true;
      }
    }
  }

  const char* term_program = std::getenv("TERM_PROGRAM");
  if (term_program != nullptr && term_program[0] != '\0') {
    const std::string program_lower = to_lower(term_program);
    static const char* const kLikelyPrograms[] = {"kitty", "wezterm", "ghostty", "iterm",
                                                  "tabby", "rio"};
    for (const char* hint : kLikelyPrograms) {
      if (program_lower.find(hint) != std::string::npos) {
        return true;
      }
    }
  }

  return false;
}

bool auto_should_use_nerd() {
  if (!locale_is_utf8()) {
    return false;
  }
  if (nerd_font_hint_env()) {
    return true;
  }
  return terminal_likely_has_nerd_font();
}

IconMode parse_icon_mode_string(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return IconMode::Auto;
  }
  const std::string mode = to_lower(value);
  if (mode == "always" || mode == "1" || mode == "yes" || mode == "on" || mode == "nerd") {
    return IconMode::Always;
  }
  if (mode == "never" || mode == "0" || mode == "no" || mode == "off" || mode == "ascii") {
    return IconMode::Never;
  }
  return IconMode::Auto;
}

bool should_use_nerd(IconMode mode) {
  switch (mode) {
    case IconMode::Always:
      return true;
    case IconMode::Never:
      return false;
    case IconMode::Auto:
    default:
      return auto_should_use_nerd();
  }
}

const GlyphPair& symbol_glyph_pair(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kNamespace:
      return kSymbolGlyphs[0];
    case SymbolKind::kClass:
      return kSymbolGlyphs[1];
    case SymbolKind::kStruct:
      return kSymbolGlyphs[2];
    case SymbolKind::kMethod:
      return kSymbolGlyphs[3];
    case SymbolKind::kVariable:
      return kSymbolGlyphs[4];
    case SymbolKind::kFunction:
    default:
      return kSymbolGlyphs[5];
  }
}

bool ends_with_insensitive(const std::string& value, const std::string& suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  const std::size_t offset = value.size() - suffix.size();
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[offset + i])) !=
        std::tolower(static_cast<unsigned char>(suffix[i]))) {
      return false;
    }
  }
  return true;
}

bool is_makefile_name(const std::string& lower_name) {
  return lower_name == "makefile" || lower_name == "gnumakefile" ||
         ends_with_insensitive(lower_name, ".mk");
}

}  // namespace

IconMode parse_icon_mode_env() {
  return parse_icon_mode_string(std::getenv("TGDB_ICONS"));
}

IconMode resolve_icon_mode(IconMode settings_mode) {
  const IconMode env_mode = parse_icon_mode_env();
  const char* raw = std::getenv("TGDB_ICONS");
  if (raw != nullptr && raw[0] != '\0') {
    return env_mode;
  }
  return settings_mode;
}

void configure_glyphs(IconMode mode) {
  configured_mode_ = mode;
  use_nerd_ = should_use_nerd(mode);
  configured_ = true;
}

bool glyphs_use_nerd() {
  if (!configured_) {
    configure_glyphs(IconMode::Auto);
  }
  return use_nerd_;
}

std::string symbol_kind_glyph(SymbolKind kind) {
  const GlyphPair& pair = symbol_glyph_pair(kind);
  return glyphs_use_nerd() ? pair.nerd : pair.ascii;
}

std::string strip_symbol_kind_prefix(const std::string& prefixed_name) {
  for (const char* prefix : kSymbolKindPrefixes) {
    const std::size_t len = std::strlen(prefix);
    if (prefixed_name.size() >= len && prefixed_name.compare(0, len, prefix) == 0) {
      return prefixed_name.substr(len);
    }
  }
  return prefixed_name;
}

std::string file_glyph(const std::string& filename) {
  const std::string lower = to_lower(filename);
  const auto pick = [](const char* nerd, const char* ascii) {
    return std::string(glyphs_use_nerd() ? nerd : ascii);
  };

  if (lower == "cmakelists.txt" || ends_with_insensitive(lower, ".cmake")) {
    return pick(kFileCmake, "cm");
  }
  if (is_makefile_name(lower)) {
    return pick(kFileMakefile, "mk");
  }
  if (ends_with_insensitive(lower, ".cpp") || ends_with_insensitive(lower, ".cc") ||
      ends_with_insensitive(lower, ".cxx") || ends_with_insensitive(lower, ".c")) {
    return pick(kFileCpp, "++");
  }
  if (ends_with_insensitive(lower, ".h") || ends_with_insensitive(lower, ".hpp") ||
      ends_with_insensitive(lower, ".hh") || ends_with_insensitive(lower, ".hxx")) {
    return pick(kFileHeader, "h");
  }
  if (ends_with_insensitive(lower, ".json")) {
    return pick(kFileJson, "{}");
  }
  if (ends_with_insensitive(lower, ".md") || ends_with_insensitive(lower, ".markdown")) {
    return pick(kFileMarkdown, "md");
  }
  return pick(kFileDefault, "·");
}

int utf8_codepoint_count(const std::string& text) {
  int count = 0;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char byte = static_cast<unsigned char>(text[i]);
    std::size_t step = 1;
    if ((byte & 0x80) == 0) {
      step = 1;
    } else if ((byte & 0xE0) == 0xC0) {
      step = 2;
    } else if ((byte & 0xF0) == 0xE0) {
      step = 3;
    } else if ((byte & 0xF8) == 0xF0) {
      step = 4;
    }
    i += step;
    ++count;
  }
  return count;
}

std::string file_glyph_display(const std::string& filename) {
  const std::string glyph = file_glyph(filename);
  if (glyphs_use_nerd()) {
    return glyph;
  }
  if (utf8_codepoint_count(glyph) >= 2) {
    return glyph;
  }
  return glyph + " ";
}

std::string folder_glyph(bool expanded) {
  if (glyphs_use_nerd()) {
    return expanded ? kFolderOpen : kFolderClosed;
  }
  return expanded ? "v" : ">";
}

}  // namespace tgdb
