#include "ui/theme.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace tgdb::theme {

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
    Color::RGB(26, 28, 36),     Color::RGB(90, 170, 255),
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
    Color::RGB(235, 238, 245),  Color::RGB(0, 102, 204),
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

const Palette& current_palette() {
  return g_mode == ThemeMode::kLight ? kLightPalette : kDarkPalette;
}

Color from_rgb(const ColorRgb& rgb_color) { return Color::RGB(rgb_color.r, rgb_color.g, rgb_color.b); }

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
  switch (preset) {
    case UiColorPreset::kDarkClassic:
      return UiColorOverrides{
          rgb(0x1c, 0x20, 0x2a), rgb(0x00, 0x00, 0x00), rgb(0xb4, 0xc8, 0xff),
          rgb(0x5a, 0xaa, 0xff), rgb(0xb4, 0xc8, 0xff), rgb(0x82, 0x8c, 0xa0),
      };
    case UiColorPreset::kDarkSoft:
      return UiColorOverrides{
          rgb(0x2d, 0x2d, 0x30), rgb(0x1e, 0x1e, 0x1e), rgb(0xd4, 0xd4, 0xd4),
          rgb(0x9c, 0xdc, 0xfe), rgb(0xcc, 0xcc, 0xcc), rgb(0x85, 0x85, 0x85),
      };
    case UiColorPreset::kLightClassic:
      return UiColorOverrides{
          rgb(0xf5, 0xf5, 0xf8), rgb(0xff, 0xff, 0xff), rgb(0x1e, 0x28, 0x3c),
          rgb(0x00, 0x66, 0xcc), rgb(0x1e, 0x28, 0x3c), rgb(0x64, 0x6e, 0x82),
      };
    case UiColorPreset::kLightPaper:
      return UiColorOverrides{
          rgb(0xec, 0xea, 0xe4), rgb(0xfa, 0xf9, 0xf5), rgb(0x2b, 0x2b, 0x28),
          rgb(0x00, 0x5a, 0x9e), rgb(0x3a, 0x3a, 0x36), rgb(0x70, 0x70, 0x68),
      };
    case UiColorPreset::kCustom:
      break;
  }
  return {};
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

void set_mode(ThemeMode mode) { g_mode = mode; }

ThemeMode current_mode() { return g_mode; }

void set_ui_overrides(const UiColorOverrides& overrides) { g_overrides = overrides; }

const UiColorOverrides& current_ui_overrides() { return g_overrides; }

void clear_ui_overrides() { g_overrides = {}; }

UiColorOverrides overrides_for_preset(UiColorPreset preset) {
  return make_preset_overrides(preset);
}

ThemeMode theme_mode_for_preset(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kLightClassic:
    case UiColorPreset::kLightPaper:
      return ThemeMode::kLight;
    case UiColorPreset::kDarkClassic:
    case UiColorPreset::kDarkSoft:
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
    case UiColorPreset::kCustom:
      return "custom";
  }
  return "custom";
}

const char* ui_color_preset_label(UiColorPreset preset) {
  switch (preset) {
    case UiColorPreset::kDarkClassic:
      return "Oscuro clásico";
    case UiColorPreset::kDarkSoft:
      return "Oscuro suave";
    case UiColorPreset::kLightClassic:
      return "Claro clásico";
    case UiColorPreset::kLightPaper:
      return "Claro papel";
    case UiColorPreset::kCustom:
      return "Personalizado";
  }
  return "Personalizado";
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
  if (g_overrides.code_bg) {
    return from_rgb(brighten(*g_overrides.code_bg, 12));
  }
  return current_palette().editor_line_hi;
}

Color CursorCell() { return current_palette().cursor_cell; }
Color SelectionBg() { return current_palette().selection_bg; }
Color FindMatchBg() { return current_palette().find_match_bg; }
Color SelectionOccurrenceBg() {
  const ColorRgb code = effective_code_bg_rgb();
  const ColorRgb tint = g_mode == ThemeMode::kLight ? rgb(210, 225, 245) : rgb(55, 75, 110);
  return from_rgb(blend(code, tint, 0.45f));
}
Color BracketMatchBg() { return current_palette().bracket_match_bg; }

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

}  // namespace tgdb::theme
