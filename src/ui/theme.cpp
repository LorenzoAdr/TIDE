#include "ui/theme.hpp"

#include "i18n/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string>

namespace tuide::theme {

namespace {

struct Palette {
  Color accent;
  Color accent_dim;
  Color play;
  Color pause;
  Color stop;
  ColorRgb panel_bg_rgb;
  ColorRgb code_bg_rgb;
  ColorRgb accent_rgb;
  ColorRgb header_rgb;
  ColorRgb muted_rgb;
  Color panel_bg;
  Color code_bg;
  Color editor_line_hi;
  Color cursor_cell;
  Color selection_bg;
  Color find_match_bg;
  Color bracket_match_bg;
  Color tab_active;
  Color tab_hover;
  Color tab_pressed;
  Color tab_idle;
  Color header;
  Color muted;
  Color watch_input;
  Color bp_active;
  Color bp_disabled;
  Color error;
  Color warning;
  Color success;
  Color stack_frame;
  Color status_bar;
  Color syntax_default;
  Color syntax_comment;
  Color syntax_string;
  Color syntax_number;
  Color syntax_keyword;
  Color syntax_type;
  Color syntax_function;
  Color syntax_variable;
  Color syntax_parameter;
  Color syntax_property;
  Color syntax_macro;
  Color syntax_namespace;
  Color syntax_operator;
  Color build_file_line_bg;
  Color build_file_keyword;
};

ColorRgb rgb(uint8_t r, uint8_t g, uint8_t b) { return ColorRgb{r, g, b}; }

const Palette kDarkPalette{
    Color::RGB(90, 170, 255),    Color::RGB(50, 90, 140),    Color::RGB(80, 220, 120),
    Color::RGB(255, 210, 80),    Color::RGB(255, 90, 90),
    rgb(28, 32, 42),             rgb(0, 0, 0),               rgb(90, 170, 255),
    rgb(180, 200, 255),          rgb(130, 140, 160),
    Color::RGB(28, 32, 42),      Color::RGB(0, 0, 0),
    Color::RGB(42, 46, 62),     Color::RGB(90, 170, 255),
    Color::RGB(60, 70, 100),     Color::RGB(80, 70, 30),     Color::RGB(45, 70, 55),
    Color::RGB(55, 75, 110),     Color::RGB(48, 58, 72),     Color::RGB(70, 95, 130),
    Color::RGB(38, 42, 52),      Color::RGB(180, 200, 255),  Color::RGB(130, 140, 160),
    Color::RGB(200, 230, 255),   Color::RGB(255, 120, 120),  Color::RGB(100, 100, 110),
    Color::RGB(255, 100, 100),   Color::RGB(255, 200, 80),   Color::RGB(80, 220, 120),
    Color::RGB(170, 210, 255),   Color::RGB(35, 45, 65),      Color::RGB(220, 223, 228),  Color::RGB(106, 153, 85),
    Color::RGB(206, 145, 120),   Color::RGB(181, 206, 168),  Color::RGB(198, 120, 221),
    Color::RGB(78, 201, 176),    Color::RGB(220, 220, 170),  Color::RGB(156, 220, 254),
    Color::RGB(156, 200, 254),   Color::RGB(156, 220, 200),  Color::RGB(205, 170, 255),
    Color::RGB(130, 170, 255),   Color::RGB(180, 180, 210),
    Color::RGB(38, 34, 28),      Color::RGB(220, 175, 110),
};

const Palette kLightPalette{
    Color::RGB(0, 102, 204),     Color::RGB(180, 200, 230),  Color::RGB(0, 140, 60),
    Color::RGB(180, 120, 0),     Color::RGB(200, 40, 40),
    rgb(245, 245, 248),          rgb(255, 255, 255),         rgb(0, 102, 204),
    rgb(30, 40, 60),             rgb(100, 110, 130),
    Color::RGB(245, 245, 248),   Color::RGB(255, 255, 255),
    Color::RGB(216, 224, 240),  Color::RGB(0, 102, 204),
    Color::RGB(180, 200, 255),   Color::RGB(255, 240, 180),  Color::RGB(200, 235, 210),
    Color::RGB(210, 225, 245),   Color::RGB(225, 230, 240),  Color::RGB(190, 210, 235),
    Color::RGB(220, 224, 232),   Color::RGB(30, 40, 60),     Color::RGB(100, 110, 130),
    Color::RGB(20, 50, 90),      Color::RGB(200, 40, 40),     Color::RGB(160, 160, 170),
    Color::RGB(200, 40, 40),     Color::RGB(180, 100, 0),    Color::RGB(0, 140, 60),
    Color::RGB(0, 80, 180),      Color::RGB(230, 235, 245),   Color::RGB(36, 41, 46),     Color::RGB(0, 128, 0),
    Color::RGB(163, 21, 21),     Color::RGB(9, 134, 88),     Color::RGB(175, 0, 157),
    Color::RGB(0, 120, 100),     Color::RGB(121, 94, 38),    Color::RGB(0, 16, 128),
    Color::RGB(0, 90, 120),      Color::RGB(0, 100, 80),     Color::RGB(130, 50, 180),
    Color::RGB(0, 70, 160),      Color::RGB(60, 60, 90),
    Color::RGB(252, 246, 236),   Color::RGB(150, 90, 30),
};

ThemeMode g_mode = ThemeMode::kDark;
UiColorOverrides g_overrides;
UiColorPreset g_color_preset = UiColorPreset::kDarkClassic;
std::optional<Palette> g_preset_palette;
uint64_t g_colors_revision = 1;

void bump_colors_revision() { ++g_colors_revision; }

constexpr UiColorPreset kListedPresets[] = {
    UiColorPreset::kDarkClassic,  UiColorPreset::kDarkSoft,      UiColorPreset::kNord,
    UiColorPreset::kGruvboxDark,  UiColorPreset::kOneDark,       UiColorPreset::kDracula,
    UiColorPreset::kMonokai,      UiColorPreset::kTokyoNight,    UiColorPreset::kLightClassic,
    UiColorPreset::kLightPaper,   UiColorPreset::kGruvboxLight,  UiColorPreset::kSolarizedLight,
};

int luminance(const ColorRgb& color) {
  return (static_cast<int>(color.r) * 299 + static_cast<int>(color.g) * 587 +
          static_cast<int>(color.b) * 114) /
         1000;
}

Color from_rgb(const ColorRgb& rgb_color) { return Color::RGB(rgb_color.r, rgb_color.g, rgb_color.b); }

Palette with_ui_rgb(Palette palette, ColorRgb panel_bg, ColorRgb code_bg, ColorRgb accent_rgb,
                    ColorRgb header_rgb, ColorRgb muted_rgb) {
  palette.panel_bg_rgb = panel_bg;
  palette.code_bg_rgb = code_bg;
  palette.accent_rgb = accent_rgb;
  palette.header_rgb = header_rgb;
  palette.muted_rgb = muted_rgb;
  palette.panel_bg = from_rgb(panel_bg);
  palette.code_bg = from_rgb(code_bg);
  palette.header = from_rgb(header_rgb);
  palette.muted = from_rgb(muted_rgb);
  palette.watch_input = from_rgb(header_rgb);
  palette.stack_frame = from_rgb(header_rgb);
  palette.syntax_default = from_rgb(header_rgb);
  return palette;
}

Palette with_syntax(Palette palette, ColorRgb def, ColorRgb comment, ColorRgb str, ColorRgb number,
                    ColorRgb keyword, ColorRgb type, ColorRgb function, ColorRgb variable,
                    ColorRgb parameter, ColorRgb property, ColorRgb macro, ColorRgb ns,
                    ColorRgb op) {
  palette.syntax_default = from_rgb(def);
  palette.syntax_comment = from_rgb(comment);
  palette.syntax_string = from_rgb(str);
  palette.syntax_number = from_rgb(number);
  palette.syntax_keyword = from_rgb(keyword);
  palette.syntax_type = from_rgb(type);
  palette.syntax_function = from_rgb(function);
  palette.syntax_variable = from_rgb(variable);
  palette.syntax_parameter = from_rgb(parameter);
  palette.syntax_property = from_rgb(property);
  palette.syntax_macro = from_rgb(macro);
  palette.syntax_namespace = from_rgb(ns);
  palette.syntax_operator = from_rgb(op);
  return palette;
}

Palette preset_palette(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kDarkClassic:
      return kDarkPalette;
    case UiColorPreset::kDarkSoft:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x2d, 0x2d, 0x30), rgb(0x1e, 0x1e, 0x1e),
                      rgb(0x9c, 0xdc, 0xfe), rgb(0xd4, 0xd4, 0xd4), rgb(0x85, 0x85, 0x85)),
          rgb(0xd4, 0xd4, 0xd4), rgb(0x6a, 0x99, 0x55), rgb(0xce, 0x91, 0x78), rgb(0xb5, 0xce, 0xa8),
          rgb(0x56, 0x9c, 0xd6), rgb(0x4e, 0xc9, 0xb0), rgb(0xd4, 0xdc, 0xeb), rgb(0x9d, 0xdc, 0xfe),
          rgb(0x9d, 0xdc, 0xfe), rgb(0x4f, 0xc1, 0xff), rgb(0xc5, 0x86, 0xc0), rgb(0x4e, 0xc9, 0xb0),
          rgb(0xd4, 0xd4, 0xd4));
    case UiColorPreset::kNord:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x2e, 0x34, 0x40), rgb(0x2e, 0x34, 0x40),
                      rgb(0x88, 0xc0, 0xd0), rgb(0xd8, 0xde, 0xe9), rgb(0x81, 0xa1, 0xc1)),
          rgb(0xd8, 0xde, 0xe9), rgb(0x61, 0x6e, 0x88), rgb(0xa3, 0xbe, 0x8c), rgb(0xb4, 0x8e, 0xad),
          rgb(0x81, 0xa1, 0xc1), rgb(0x8f, 0xbc, 0xbb), rgb(0x88, 0xc0, 0xd0), rgb(0xd8, 0xde, 0xe9),
          rgb(0xd8, 0xde, 0xe9), rgb(0x5e, 0x81, 0xac), rgb(0xb4, 0x8e, 0xad), rgb(0x8f, 0xbc, 0xbb),
          rgb(0xd8, 0xde, 0xe9));
    case UiColorPreset::kGruvboxDark:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x28, 0x28, 0x28), rgb(0x1d, 0x20, 0x21),
                      rgb(0x83, 0xa5, 0x98), rgb(0xeb, 0xdb, 0xb2), rgb(0xa8, 0x99, 0x84)),
          rgb(0xeb, 0xdb, 0xb2), rgb(0x92, 0x83, 0x74), rgb(0xb8, 0xbb, 0x26), rgb(0xd3, 0x86, 0x9b),
          rgb(0xfb, 0x49, 0x34), rgb(0xfa, 0xbd, 0x2f), rgb(0x83, 0xa5, 0x98), rgb(0xeb, 0xdb, 0xb2),
          rgb(0xeb, 0xdb, 0xb2), rgb(0xfe, 0x80, 0x19), rgb(0xd3, 0x86, 0x9b), rgb(0x8e, 0xc0, 0x7c),
          rgb(0xeb, 0xdb, 0xb2));
    case UiColorPreset::kOneDark:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x28, 0x2c, 0x34), rgb(0x28, 0x2c, 0x34),
                      rgb(0x61, 0xaf, 0xef), rgb(0xab, 0xb2, 0xbf), rgb(0x5c, 0x63, 0x70)),
          rgb(0xab, 0xb2, 0xbf), rgb(0x5c, 0x63, 0x70), rgb(0x98, 0xc3, 0x79), rgb(0xd1, 0x9a, 0x66),
          rgb(0xc6, 0x78, 0xdd), rgb(0xe5, 0xc0, 0x7b), rgb(0x61, 0xaf, 0xef), rgb(0xe0, 0x6c, 0x75),
          rgb(0xab, 0xb2, 0xbf), rgb(0xe0, 0x6c, 0x75), rgb(0xc6, 0x78, 0xdd), rgb(0x56, 0xb6, 0xc2),
          rgb(0xab, 0xb2, 0xbf));
    case UiColorPreset::kDracula:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x28, 0x2a, 0x36), rgb(0x28, 0x2a, 0x36),
                      rgb(0xbd, 0x93, 0xf9), rgb(0xf8, 0xf8, 0xf2), rgb(0x62, 0x72, 0xa4)),
          rgb(0xf8, 0xf8, 0xf2), rgb(0x62, 0x7a, 0xa0), rgb(0xf1, 0xfa, 0x8c), rgb(0xbd, 0x93, 0xf9),
          rgb(0xff, 0x79, 0xc6), rgb(0x8b, 0xe9, 0xfd), rgb(0x50, 0xfa, 0x7b), rgb(0xff, 0x55, 0x55),
          rgb(0xff, 0xb8, 0x6c), rgb(0xff, 0x55, 0x55), rgb(0xff, 0x79, 0xc6), rgb(0x8b, 0xe9, 0xfd),
          rgb(0xf8, 0xf8, 0xf2));
    case UiColorPreset::kMonokai:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x27, 0x28, 0x22), rgb(0x27, 0x28, 0x22),
                      rgb(0x66, 0xd9, 0xef), rgb(0xf8, 0xf8, 0xf2), rgb(0x75, 0x71, 0x5e)),
          rgb(0xf8, 0xf8, 0xf2), rgb(0x75, 0x71, 0x5e), rgb(0xe6, 0xdb, 0x74), rgb(0xae, 0x81, 0xff),
          rgb(0xf9, 0x26, 0x72), rgb(0xfd, 0x97, 0x1f), rgb(0xa6, 0xe2, 0x2e), rgb(0xf9, 0x26, 0x72),
          rgb(0xfd, 0x97, 0x1f), rgb(0xf9, 0x26, 0x72), rgb(0xae, 0x81, 0xff), rgb(0x66, 0xd9, 0xef),
          rgb(0xf8, 0xf8, 0xf2));
    case UiColorPreset::kTokyoNight:
      return with_syntax(
          with_ui_rgb(kDarkPalette, rgb(0x1a, 0x1b, 0x26), rgb(0x1a, 0x1b, 0x26),
                      rgb(0x7a, 0xa2, 0xf7), rgb(0xc0, 0xca, 0xf5), rgb(0x56, 0x5f, 0x89)),
          rgb(0xc0, 0xca, 0xf5), rgb(0x56, 0x5f, 0x89), rgb(0x9e, 0xce, 0x6a), rgb(0xff, 0x9e, 0x64),
          rgb(0xbb, 0x9a, 0xf7), rgb(0x2a, 0xc3, 0xde), rgb(0x7a, 0xa2, 0xf7), rgb(0xf7, 0x76, 0x8e),
          rgb(0xc0, 0xca, 0xf5), rgb(0xf7, 0x76, 0x8e), rgb(0xbb, 0x9a, 0xf7), rgb(0x2a, 0xc3, 0xde),
          rgb(0xc0, 0xca, 0xf5));
    case UiColorPreset::kLightClassic:
      return kLightPalette;
    case UiColorPreset::kLightPaper:
      return with_syntax(
          with_ui_rgb(kLightPalette, rgb(0xec, 0xea, 0xe4), rgb(0xfa, 0xf9, 0xf5),
                      rgb(0x00, 0x5a, 0x9e), rgb(0x2b, 0x2b, 0x28), rgb(0x70, 0x70, 0x68)),
          rgb(0x2b, 0x2b, 0x28), rgb(0x96, 0x96, 0x90), rgb(0x0d, 0x73, 0x45), rgb(0x9a, 0x34, 0x00),
          rgb(0x00, 0x5a, 0x9e), rgb(0x8b, 0x5e, 0x00), rgb(0x00, 0x5a, 0x9e), rgb(0x9a, 0x34, 0x00),
          rgb(0x2b, 0x2b, 0x28), rgb(0x9a, 0x34, 0x00), rgb(0x5a, 0x00, 0x7a), rgb(0x00, 0x6d, 0x6d),
          rgb(0x2b, 0x2b, 0x28));
    case UiColorPreset::kGruvboxLight:
      return with_syntax(
          with_ui_rgb(kLightPalette, rgb(0xeb, 0xdb, 0xb2), rgb(0xfb, 0xf1, 0xc7),
                      rgb(0x45, 0x7b, 0x6c), rgb(0x3c, 0x38, 0x36), rgb(0x7c, 0x6f, 0x64)),
          rgb(0x3c, 0x38, 0x36), rgb(0x92, 0x83, 0x74), rgb(0x79, 0x7a, 0x1a), rgb(0x8f, 0x3f, 0x71),
          rgb(0x9d, 0x00, 0x06), rgb(0xb5, 0x76, 0x14), rgb(0x45, 0x7b, 0x6c), rgb(0x9d, 0x00, 0x06),
          rgb(0x3c, 0x38, 0x36), rgb(0xaf, 0x3a, 0x03), rgb(0x8f, 0x3f, 0x71), rgb(0x42, 0x7b, 0x58),
          rgb(0x3c, 0x38, 0x36));
    case UiColorPreset::kSolarizedLight:
      return with_syntax(
          with_ui_rgb(kLightPalette, rgb(0xee, 0xe8, 0xd5), rgb(0xfd, 0xf6, 0xe3),
                      rgb(0x26, 0x8b, 0xd2), rgb(0x65, 0x7b, 0x83), rgb(0x93, 0xa1, 0xa1)),
          rgb(0x65, 0x7b, 0x83), rgb(0x93, 0xa1, 0xa1), rgb(0x2a, 0xa1, 0x98), rgb(0xd3, 0x36, 0x82),
          rgb(0x26, 0x8b, 0xd2), rgb(0xb5, 0x89, 0x00), rgb(0x26, 0x8b, 0xd2), rgb(0xdc, 0x32, 0x2f),
          rgb(0x65, 0x7b, 0x83), rgb(0xcb, 0x4b, 0x16), rgb(0xd3, 0x36, 0x82), rgb(0x2a, 0xa1, 0x98),
          rgb(0x65, 0x7b, 0x83));
    case UiColorPreset::kCustom:
      break;
  }
  return g_mode == ThemeMode::kLight ? kLightPalette : kDarkPalette;
}

const Palette& current_palette() {
  if (g_color_preset != UiColorPreset::kCustom && g_preset_palette.has_value()) {
    return *g_preset_palette;
  }
  if (g_overrides.code_bg) {
    return luminance(*g_overrides.code_bg) > 140 ? kLightPalette : kDarkPalette;
  }
  return g_mode == ThemeMode::kLight ? kLightPalette : kDarkPalette;
}

ColorRgb brighten(const ColorRgb& color, int delta) {
  auto clamp = [](int value) {
    return static_cast<uint8_t>(std::max(0, std::min(255, value)));
  };
  return ColorRgb{clamp(static_cast<int>(color.r) + delta),
                  clamp(static_cast<int>(color.g) + delta),
                  clamp(static_cast<int>(color.b) + delta)};
}

ColorRgb blend(const ColorRgb& a, const ColorRgb& b, float t) {
  auto mix = [&](uint8_t x, uint8_t y) {
    return static_cast<uint8_t>(static_cast<int>(x * (1.0f - t) + y * t));
  };
  return ColorRgb{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
}

ColorRgb dim(const ColorRgb& color, float factor) {
  return ColorRgb{static_cast<uint8_t>(static_cast<int>(color.r) * factor),
                  static_cast<uint8_t>(static_cast<int>(color.g) * factor),
                  static_cast<uint8_t>(static_cast<int>(color.b) * factor)};
}

ColorRgb effective_panel_bg_rgb() {
  if (g_overrides.panel_bg) {
    return *g_overrides.panel_bg;
  }
  return current_palette().panel_bg_rgb;
}

ColorRgb effective_code_bg_rgb() {
  if (g_overrides.code_bg) {
    return *g_overrides.code_bg;
  }
  return current_palette().code_bg_rgb;
}

ColorRgb effective_text_rgb() {
  if (g_overrides.text) {
    return *g_overrides.text;
  }
  return current_palette().header_rgb;
}

ColorRgb effective_title_rgb() {
  if (g_overrides.title) {
    return *g_overrides.title;
  }
  return current_palette().accent_rgb;
}

bool has_ui_color_overrides() { return !g_overrides.empty(); }

UiColorOverrides make_preset_overrides(UiColorPreset preset) {
  if (preset == UiColorPreset::kCustom) {
    return {};
  }
  const Palette palette = preset_palette(preset);
  return UiColorOverrides{
      palette.panel_bg_rgb, palette.code_bg_rgb, palette.header_rgb,
      palette.accent_rgb,     palette.header_rgb, palette.muted_rgb,
  };
}

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower >= 'a' && lower <= 'f') {
    return lower - 'a' + 10;
  }
  return -1;
}

}  // namespace

bool UiColorOverrides::empty() const {
  return !panel_bg && !code_bg && !text && !title && !directory && !file;
}

void set_mode(ThemeMode mode) {
  if (g_mode != mode) {
    g_mode = mode;
    bump_colors_revision();
  }
}

ThemeMode current_mode() { return g_mode; }

int listed_preset_count() {
  return static_cast<int>(sizeof(kListedPresets) / sizeof(kListedPresets[0]));
}

UiColorPreset listed_preset_at(int index) {
  if (index < 0 || index >= listed_preset_count()) {
    return UiColorPreset::kDarkClassic;
  }
  return kListedPresets[index];
}

int index_of_listed_preset(UiColorPreset preset) {
  for (int i = 0; i < listed_preset_count(); ++i) {
    if (kListedPresets[i] == preset) {
      return i;
    }
  }
  return 0;
}

void apply_color_preset(UiColorPreset preset, const UiColorOverrides& custom_overrides) {
  g_color_preset = preset;
  if (preset == UiColorPreset::kCustom) {
    g_preset_palette.reset();
    g_overrides = custom_overrides;
    if (custom_overrides.code_bg) {
      g_mode = luminance(*custom_overrides.code_bg) > 140 ? ThemeMode::kLight : ThemeMode::kDark;
    }
    bump_colors_revision();
    return;
  }
  g_preset_palette = preset_palette(preset);
  g_overrides = overrides_for_preset(preset);
  g_mode = theme_mode_for_preset(preset);
  bump_colors_revision();
}

UiColorPreset current_color_preset() { return g_color_preset; }

uint64_t colors_revision() { return g_colors_revision; }

void set_ui_overrides(const UiColorOverrides& overrides) {
  g_overrides = overrides;
  if (!overrides.empty()) {
    g_color_preset = UiColorPreset::kCustom;
    g_preset_palette.reset();
    if (overrides.code_bg) {
      g_mode = luminance(*overrides.code_bg) > 140 ? ThemeMode::kLight : ThemeMode::kDark;
    }
  }
  bump_colors_revision();
}

const UiColorOverrides& current_ui_overrides() { return g_overrides; }

void clear_ui_overrides() {
  g_overrides = {};
  bump_colors_revision();
}

UiColorOverrides overrides_for_preset(UiColorPreset preset) {
  return make_preset_overrides(preset);
}

ThemeMode theme_mode_for_preset(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kLightClassic:
    case UiColorPreset::kLightPaper:
    case UiColorPreset::kGruvboxLight:
    case UiColorPreset::kSolarizedLight:
      return ThemeMode::kLight;
    case UiColorPreset::kDarkClassic:
    case UiColorPreset::kDarkSoft:
    case UiColorPreset::kNord:
    case UiColorPreset::kGruvboxDark:
    case UiColorPreset::kOneDark:
    case UiColorPreset::kDracula:
    case UiColorPreset::kMonokai:
    case UiColorPreset::kTokyoNight:
    case UiColorPreset::kCustom:
    default:
      return ThemeMode::kDark;
  }
}

UiColorPreset parse_ui_color_preset(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "dark_classic" || lower == "oscuro_clasico") {
    return UiColorPreset::kDarkClassic;
  }
  if (lower == "dark_soft" || lower == "oscuro_suave") {
    return UiColorPreset::kDarkSoft;
  }
  if (lower == "light_classic" || lower == "claro_clasico") {
    return UiColorPreset::kLightClassic;
  }
  if (lower == "light_paper" || lower == "claro_papel") {
    return UiColorPreset::kLightPaper;
  }
  if (lower == "nord") {
    return UiColorPreset::kNord;
  }
  if (lower == "gruvbox_dark" || lower == "gruvbox") {
    return UiColorPreset::kGruvboxDark;
  }
  if (lower == "one_dark" || lower == "onedark") {
    return UiColorPreset::kOneDark;
  }
  if (lower == "dracula") {
    return UiColorPreset::kDracula;
  }
  if (lower == "monokai") {
    return UiColorPreset::kMonokai;
  }
  if (lower == "tokyo_night" || lower == "tokyonight") {
    return UiColorPreset::kTokyoNight;
  }
  if (lower == "gruvbox_light") {
    return UiColorPreset::kGruvboxLight;
  }
  if (lower == "solarized_light" || lower == "solarized") {
    return UiColorPreset::kSolarizedLight;
  }
  if (lower == "custom") {
    return UiColorPreset::kCustom;
  }
  return UiColorPreset::kCustom;
}

const char* ui_color_preset_name(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kDarkClassic:
      return "dark_classic";
    case UiColorPreset::kDarkSoft:
      return "dark_soft";
    case UiColorPreset::kLightClassic:
      return "light_classic";
    case UiColorPreset::kLightPaper:
      return "light_paper";
    case UiColorPreset::kNord:
      return "nord";
    case UiColorPreset::kGruvboxDark:
      return "gruvbox_dark";
    case UiColorPreset::kOneDark:
      return "one_dark";
    case UiColorPreset::kDracula:
      return "dracula";
    case UiColorPreset::kMonokai:
      return "monokai";
    case UiColorPreset::kTokyoNight:
      return "tokyo_night";
    case UiColorPreset::kGruvboxLight:
      return "gruvbox_light";
    case UiColorPreset::kSolarizedLight:
      return "solarized_light";
    case UiColorPreset::kCustom:
      return "custom";
  }
  return "custom";
}

std::string ui_color_preset_label(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kDarkClassic:
      return i18n::tr("theme.preset.dark_classic");
    case UiColorPreset::kDarkSoft:
      return i18n::tr("theme.preset.dark_soft");
    case UiColorPreset::kLightClassic:
      return i18n::tr("theme.preset.light_classic");
    case UiColorPreset::kLightPaper:
      return i18n::tr("theme.preset.light_paper");
    case UiColorPreset::kNord:
      return i18n::tr("theme.preset.nord");
    case UiColorPreset::kGruvboxDark:
      return i18n::tr("theme.preset.gruvbox_dark");
    case UiColorPreset::kOneDark:
      return i18n::tr("theme.preset.one_dark");
    case UiColorPreset::kDracula:
      return i18n::tr("theme.preset.dracula");
    case UiColorPreset::kMonokai:
      return i18n::tr("theme.preset.monokai");
    case UiColorPreset::kTokyoNight:
      return i18n::tr("theme.preset.tokyo_night");
    case UiColorPreset::kGruvboxLight:
      return i18n::tr("theme.preset.gruvbox_light");
    case UiColorPreset::kSolarizedLight:
      return i18n::tr("theme.preset.solarized_light");
    case UiColorPreset::kCustom:
      return i18n::tr("theme.preset.custom");
  }
  return i18n::tr("theme.preset.custom");
}

UiColorOverrides snapshot_effective_ui_colors() {
  UiColorOverrides snapshot;
  snapshot.panel_bg = effective_panel_bg_rgb();
  snapshot.code_bg = effective_code_bg_rgb();
  snapshot.text = effective_text_rgb();
  snapshot.title = effective_title_rgb();
  snapshot.directory =
      g_overrides.directory ? *g_overrides.directory : current_palette().header_rgb;
  snapshot.file = g_overrides.file ? *g_overrides.file : current_palette().muted_rgb;
  return snapshot;
}

bool parse_hex_color(const std::string& value, ColorRgb* out) {
  if (out == nullptr || value.empty()) {
    return false;
  }
  std::string hex = value;
  if (hex.front() == '#') {
    hex.erase(hex.begin());
  }
  if (hex.size() == 3) {
    hex = std::string(2, hex[0]) + std::string(2, hex[1]) + std::string(2, hex[2]);
  }
  if (hex.size() != 6) {
    return false;
  }
  int channels[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    const int hi = hex_nibble(hex[static_cast<std::size_t>(i * 2)]);
    const int lo = hex_nibble(hex[static_cast<std::size_t>(i * 2 + 1)]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    channels[i] = hi * 16 + lo;
  }
  out->r = static_cast<uint8_t>(channels[0]);
  out->g = static_cast<uint8_t>(channels[1]);
  out->b = static_cast<uint8_t>(channels[2]);
  return true;
}

std::string format_hex_color(const ColorRgb& color) {
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x", color.r, color.g, color.b);
  return buffer;
}

ThemeMode parse_theme_name(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (lower == "light" || lower == "claro") {
    return ThemeMode::kLight;
  }
  return ThemeMode::kDark;
}

const char* theme_name(ThemeMode mode) {
  return mode == ThemeMode::kLight ? "light" : "dark";
}

Color PanelBg() { return from_rgb(effective_panel_bg_rgb()); }
Color CodeBg() { return from_rgb(effective_code_bg_rgb()); }
Color UiText() { return from_rgb(effective_text_rgb()); }
Color TitleText() { return from_rgb(effective_title_rgb()); }

Color DirectoryText() {
  if (g_overrides.directory) {
    return from_rgb(*g_overrides.directory);
  }
  return from_rgb(current_palette().header_rgb);
}

Color FileText() {
  if (g_overrides.file) {
    return from_rgb(*g_overrides.file);
  }
  return from_rgb(current_palette().muted_rgb);
}

Color EditorLineHi() {
  // Always derive from the effective code background so presets/overrides keep a
  // visible caret-row highlight (static palette values alone sit too close to
  // many theme code backgrounds).
  const ColorRgb code = effective_code_bg_rgb();
  if (luminance(code) > 140) {
    return from_rgb(brighten(code, -28));
  }
  ColorRgb hi = brighten(code, 28);
  // Cool tint so the row doesn't read as flat gray on pure-black code bg.
  hi.g = static_cast<uint8_t>(std::min(255, static_cast<int>(hi.g) + 4));
  hi.b = static_cast<uint8_t>(std::min(255, static_cast<int>(hi.b) + 10));
  return from_rgb(hi);
}

Color ScopeBg(int strength_percent) {
  const int clamped = std::max(10, std::min(85, strength_percent));
  const float blend_factor = static_cast<float>(clamped) / 100.0f;
  const ColorRgb code = effective_code_bg_rgb();
  const ColorRgb tint = g_mode == ThemeMode::kLight ? rgb(214, 226, 244) : rgb(42, 52, 72);
  return from_rgb(blend(code, tint, blend_factor));
}

Color ScopeBraceBg(int gutter_strength_percent) {
  const int gutter = std::max(10, std::min(85, gutter_strength_percent));
  int brace_strength = gutter;
  if (gutter <= 45) {
    brace_strength = gutter + 14;
  } else if (gutter <= 65) {
    brace_strength = gutter + 20;
  } else {
    brace_strength = gutter + 8;
  }
  return ScopeBg(std::min(85, brace_strength));
}

Color CursorCell() { return current_palette().cursor_cell; }
Color SelectionBg() { return current_palette().selection_bg; }
Color FindMatchBg() { return current_palette().find_match_bg; }
Color SelectionOccurrenceBg() {
  const ColorRgb code = effective_code_bg_rgb();
  // Stronger tint than before so other occurrences of the selection read clearly.
  const ColorRgb tint = g_mode == ThemeMode::kLight ? rgb(160, 195, 245) : rgb(70, 110, 170);
  return from_rgb(blend(code, tint, 0.72f));
}
Color BracketMatchBg() { return current_palette().bracket_match_bg; }

Color BracePairColor(int depth) {
  static const Color kDark[] = {
      Color::RGB(255, 198, 88),   Color::RGB(178, 132, 255), Color::RGB(82, 218, 178),
      Color::RGB(255, 118, 152), Color::RGB(118, 198, 255), Color::RGB(178, 220, 98),
      Color::RGB(255, 158, 118), Color::RGB(148, 188, 255),
  };
  static const Color kLight[] = {
      Color::RGB(180, 90, 0),    Color::RGB(120, 70, 190),  Color::RGB(0, 130, 95),
      Color::RGB(200, 40, 90),   Color::RGB(0, 95, 180),    Color::RGB(90, 130, 0),
      Color::RGB(190, 80, 30),   Color::RGB(60, 90, 180),
  };
  const Color* palette = g_mode == ThemeMode::kLight ? kLight : kDark;
  constexpr int kCount = 8;
  const int index = ((depth % kCount) + kCount) % kCount;
  return palette[index];
}

Color TabIdle() {
  if (g_overrides.panel_bg) {
    return from_rgb(brighten(*g_overrides.panel_bg, 10));
  }
  return current_palette().tab_idle;
}

Color TabActive() {
  if (has_ui_color_overrides()) {
    const ColorRgb panel = effective_panel_bg_rgb();
    const ColorRgb title = effective_title_rgb();
    return from_rgb(blend(panel, title, 0.35f));
  }
  return current_palette().tab_active;
}

Color TabHover() {
  if (has_ui_color_overrides()) {
    const ColorRgb panel = effective_panel_bg_rgb();
    const ColorRgb title = effective_title_rgb();
    return from_rgb(blend(panel, title, 0.2f));
  }
  return current_palette().tab_hover;
}

Color TabPressed() { return current_palette().tab_pressed; }

Color Accent() { return TitleText(); }

Color AccentDim() {
  if (g_overrides.title || has_ui_color_overrides()) {
    return from_rgb(dim(effective_title_rgb(), 0.5f));
  }
  return current_palette().accent_dim;
}

Color IndentGuide() {
  const ColorRgb code = effective_code_bg_rgb();
  const ColorRgb tip = g_overrides.text ? dim(*g_overrides.text, 0.55f)
                                        : (g_overrides.title || has_ui_color_overrides()
                                               ? dim(effective_title_rgb(), 0.45f)
                                               : current_palette().muted_rgb);
  // ~20% toward muted/title keeps │ visible without pulling focus from syntax.
  return from_rgb(blend(code, tip, 0.20f));
}

Color Play() { return current_palette().play; }
Color Pause() { return current_palette().pause; }
Color Stop() { return current_palette().stop; }
Color Header() { return UiText(); }

Color Muted() {
  if (g_overrides.text) {
    return from_rgb(dim(*g_overrides.text, 0.65f));
  }
  return current_palette().muted;
}

Color WatchInput() { return UiText(); }
Color BpActive() { return current_palette().bp_active; }
Color BpDisabled() { return current_palette().bp_disabled; }
Color Error() { return current_palette().error; }
Color Warning() { return current_palette().warning; }
Color Success() { return current_palette().success; }
Color StackFrame() { return current_palette().stack_frame; }

Color StatusBar() {
  if (has_ui_color_overrides()) {
    const ColorRgb panel = effective_panel_bg_rgb();
    const ColorRgb title = effective_title_rgb();
    return from_rgb(blend(panel, title, 0.25f));
  }
  return current_palette().status_bar;
}

Color SyntaxDefault() { return current_palette().syntax_default; }
Color SyntaxComment() { return current_palette().syntax_comment; }
Color SyntaxString() { return current_palette().syntax_string; }
Color SyntaxNumber() { return current_palette().syntax_number; }
Color SyntaxKeyword() { return current_palette().syntax_keyword; }
Color SyntaxType() { return current_palette().syntax_type; }
Color SyntaxFunction() { return current_palette().syntax_function; }
Color SyntaxVariable() { return current_palette().syntax_variable; }
Color SyntaxParameter() { return current_palette().syntax_parameter; }
Color SyntaxProperty() { return current_palette().syntax_property; }
Color SyntaxMacro() { return current_palette().syntax_macro; }
Color SyntaxNamespace() { return current_palette().syntax_namespace; }
Color SyntaxOperator() { return current_palette().syntax_operator; }

Color ColorForSymbolKind(SymbolKind kind) {
  switch (kind) {
    case SymbolKind::kNamespace:
      return SyntaxNamespace();
    case SymbolKind::kClass:
    case SymbolKind::kStruct:
      return SyntaxType();
    case SymbolKind::kFunction:
    case SymbolKind::kMethod:
      return SyntaxFunction();
    case SymbolKind::kVariable:
      return SyntaxVariable();
  }
  return SyntaxDefault();
}

Color BuildFileLineBg() { return current_palette().build_file_line_bg; }
Color BuildFileKeyword() { return current_palette().build_file_keyword; }

}  // namespace tuide::theme
