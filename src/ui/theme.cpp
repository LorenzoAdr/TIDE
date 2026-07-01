#include "ui/theme.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace tgdb::theme {

namespace {

struct Palette {
  Color accent;
  Color accent_dim;
  Color play;
  Color pause;
  Color stop;
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

const Palette kDarkPalette{
    Color::RGB(90, 170, 255),    Color::RGB(50, 90, 140),    Color::RGB(80, 220, 120),
    Color::RGB(255, 210, 80),    Color::RGB(255, 90, 90),    Color::RGB(28, 32, 42),
    Color::RGB(0, 0, 0),         Color::RGB(26, 28, 36),     Color::RGB(90, 170, 255),
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
    Color::RGB(180, 120, 0),     Color::RGB(200, 40, 40),    Color::RGB(245, 245, 248),
    Color::RGB(255, 255, 255),   Color::RGB(235, 238, 245),  Color::RGB(0, 102, 204),
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

const Palette& current_palette() {
  return g_mode == ThemeMode::kLight ? kLightPalette : kDarkPalette;
}

}  // namespace

void set_mode(ThemeMode mode) { g_mode = mode; }

ThemeMode current_mode() { return g_mode; }

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

Color Accent() { return current_palette().accent; }
Color AccentDim() { return current_palette().accent_dim; }
Color Play() { return current_palette().play; }
Color Pause() { return current_palette().pause; }
Color Stop() { return current_palette().stop; }
Color PanelBg() { return current_palette().panel_bg; }
Color CodeBg() { return current_palette().code_bg; }
Color EditorLineHi() { return current_palette().editor_line_hi; }
Color CursorCell() { return current_palette().cursor_cell; }
Color SelectionBg() { return current_palette().selection_bg; }
Color FindMatchBg() { return current_palette().find_match_bg; }
Color BracketMatchBg() { return current_palette().bracket_match_bg; }
Color TabActive() { return current_palette().tab_active; }
Color TabHover() { return current_palette().tab_hover; }
Color TabPressed() { return current_palette().tab_pressed; }
Color TabIdle() { return current_palette().tab_idle; }
Color Header() { return current_palette().header; }
Color Muted() { return current_palette().muted; }
Color WatchInput() { return current_palette().watch_input; }
Color BpActive() { return current_palette().bp_active; }
Color BpDisabled() { return current_palette().bp_disabled; }
Color Error() { return current_palette().error; }
Color Warning() { return current_palette().warning; }
Color Success() { return current_palette().success; }
Color StackFrame() { return current_palette().stack_frame; }
Color StatusBar() { return current_palette().status_bar; }
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
Color BuildFileLineBg() { return current_palette().build_file_line_bg; }
Color BuildFileKeyword() { return current_palette().build_file_keyword; }

}  // namespace tgdb::theme
