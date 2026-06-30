#pragma once

#include "ftxui/dom/elements.hpp"

namespace tgdb::theme {

using ftxui::Color;

enum class ThemeMode {
  kDark,
  kLight,
};

void set_mode(ThemeMode mode);
ThemeMode current_mode();

Color Accent();
Color AccentDim();
Color Play();
Color Pause();
Color Stop();
Color PanelBg();
Color CodeBg();
Color EditorLineHi();
Color CursorCell();
Color SelectionBg();
Color FindMatchBg();
Color BracketMatchBg();
Color TabActive();
Color TabHover();
Color TabPressed();
Color TabIdle();
Color Header();
Color Muted();
Color WatchInput();
Color BpActive();
Color BpDisabled();
Color Error();
Color Warning();
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

ThemeMode parse_theme_name(const std::string& name);
const char* theme_name(ThemeMode mode);

}  // namespace tgdb::theme
