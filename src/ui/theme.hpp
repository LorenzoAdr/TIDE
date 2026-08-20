#pragma once

#include <optional>
#include <string>

#include "ftxui/dom/elements.hpp"
#include "symbols/symbol_kind.hpp"

namespace tuide::theme {

using ftxui::Color;

enum class ThemeMode {
  kDark,
  kLight,
};

struct ColorRgb {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

struct UiColorOverrides {
  std::optional<ColorRgb> panel_bg;
  std::optional<ColorRgb> code_bg;
  std::optional<ColorRgb> text;
  std::optional<ColorRgb> title;
  std::optional<ColorRgb> directory;
  std::optional<ColorRgb> file;

  bool empty() const;
};

enum class UiColorPreset {
  kDarkClassic,
  kDarkSoft,
  kNord,
  kGruvboxDark,
  kOneDark,
  kDracula,
  kMonokai,
  kTokyoNight,
  kLightClassic,
  kLightPaper,
  kGruvboxLight,
  kSolarizedLight,
  kCustom,
};

constexpr int kListedPresetCount = 12;

int listed_preset_count();
UiColorPreset listed_preset_at(int index);
int index_of_listed_preset(UiColorPreset preset);

void set_mode(ThemeMode mode);
ThemeMode current_mode();

void set_ui_overrides(const UiColorOverrides& overrides);
const UiColorOverrides& current_ui_overrides();
void clear_ui_overrides();

void apply_color_preset(UiColorPreset preset, const UiColorOverrides& custom_overrides = {});
UiColorPreset current_color_preset();
uint64_t colors_revision();

UiColorOverrides overrides_for_preset(UiColorPreset preset);
ThemeMode theme_mode_for_preset(UiColorPreset preset);
UiColorPreset parse_ui_color_preset(const std::string& name);
const char* ui_color_preset_name(UiColorPreset preset);
std::string ui_color_preset_label(UiColorPreset preset);

UiColorOverrides snapshot_effective_ui_colors();
bool parse_hex_color(const std::string& value, ColorRgb* out);
std::string format_hex_color(const ColorRgb& color);

Color PanelBg();
Color CodeBg();
Color UiText();
Color TitleText();
Color DirectoryText();
Color FileText();
Color EditorLineHi();
Color ScopeBg(int strength_percent = 58);
Color ScopeBraceBg(int gutter_strength_percent = 58);
Color CursorCell();
Color SelectionBg();
Color FindMatchBg();
Color SelectionOccurrenceBg();
Color BracketMatchBg();
Color BracePairColor(int depth);
Color TabActive();
Color TabHover();
Color TabPressed();
Color TabIdle();

Color Accent();
Color AccentDim();
// Editor indent guides: softer than AccentDim so they don't compete with code.
Color IndentGuide();
Color Play();
Color Pause();
Color Stop();
Color Header();
Color Muted();
Color WatchInput();
Color BpActive();
Color BpDisabled();
Color Error();
Color Warning();
Color Success();
Color StackFrame();
Color StatusBar();

Color SyntaxDefault();
Color SyntaxComment();
Color SyntaxString();
Color SyntaxNumber();
Color SyntaxKeyword();
Color SyntaxType();
Color SyntaxFunction();
Color SyntaxVariable();
Color SyntaxParameter();
Color SyntaxProperty();
Color SyntaxMacro();
Color SyntaxNamespace();
Color SyntaxOperator();

Color ColorForSymbolKind(SymbolKind kind);

Color BuildFileLineBg();
Color BuildFileKeyword();

ThemeMode parse_theme_name(const std::string& name);
const char* theme_name(ThemeMode mode);

}  // namespace tuide::theme
