#pragma once

#include "ftxui/dom/elements.hpp"

namespace tgdb::theme {

using ftxui::Color;

inline Color Accent() { return Color::RGB(90, 170, 255); }
inline Color AccentDim() { return Color::RGB(50, 90, 140); }
inline Color Play() { return Color::RGB(80, 220, 120); }
inline Color Pause() { return Color::RGB(255, 210, 80); }
inline Color Stop() { return Color::RGB(255, 90, 90); }
inline Color PanelBg() { return Color::RGB(28, 32, 42); }
inline Color CodeBg() { return Color::RGB(0, 0, 0); }
inline Color EditorLineHi() { return Color::RGB(26, 28, 36); }
inline Color CursorCell() { return Color::RGB(90, 170, 255); }
inline Color SelectionBg() { return Color::RGB(60, 70, 100); }
inline Color FindMatchBg() { return Color::RGB(80, 70, 30); }
inline Color BracketMatchBg() { return Color::RGB(45, 70, 55); }
inline Color TabActive() { return Color::RGB(55, 75, 110); }
inline Color TabHover() { return Color::RGB(48, 58, 72); }
inline Color TabPressed() { return Color::RGB(70, 95, 130); }
inline Color TabIdle() { return Color::RGB(38, 42, 52); }
inline Color Header() { return Color::RGB(180, 200, 255); }
inline Color Muted() { return Color::RGB(130, 140, 160); }
inline Color WatchInput() { return Color::RGB(200, 230, 255); }
inline Color BpActive() { return Color::RGB(255, 120, 120); }
inline Color BpDisabled() { return Color::RGB(100, 100, 110); }
inline Color Error() { return Color::RGB(255, 100, 100); }
inline Color Warning() { return Color::RGB(255, 200, 80); }
inline Color StackFrame() { return Color::RGB(170, 210, 255); }
inline Color StatusBar() { return Color::RGB(35, 45, 65); }

inline Color SyntaxDefault() { return Color::RGB(220, 223, 228); }
inline Color SyntaxComment() { return Color::RGB(106, 153, 85); }
inline Color SyntaxString() { return Color::RGB(206, 145, 120); }
inline Color SyntaxNumber() { return Color::RGB(181, 206, 168); }
inline Color SyntaxKeyword() { return Color::RGB(198, 120, 221); }
inline Color SyntaxType() { return Color::RGB(78, 201, 176); }
inline Color SyntaxFunction() { return Color::RGB(220, 220, 170); }
inline Color SyntaxVariable() { return Color::RGB(156, 220, 254); }
inline Color SyntaxParameter() { return Color::RGB(156, 200, 254); }
inline Color SyntaxProperty() { return Color::RGB(156, 220, 200); }
inline Color SyntaxMacro() { return Color::RGB(205, 170, 255); }
inline Color SyntaxNamespace() { return Color::RGB(130, 170, 255); }
inline Color SyntaxOperator() { return Color::RGB(180, 180, 210); }

}  // namespace tgdb::theme
