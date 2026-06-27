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
inline Color TabActive() { return Color::RGB(55, 75, 110); }
inline Color TabIdle() { return Color::RGB(38, 42, 52); }
inline Color Header() { return Color::RGB(180, 200, 255); }
inline Color Muted() { return Color::RGB(130, 140, 160); }
inline Color WatchInput() { return Color::RGB(200, 230, 255); }
inline Color BpActive() { return Color::RGB(255, 120, 120); }
inline Color BpDisabled() { return Color::RGB(100, 100, 110); }
inline Color StackFrame() { return Color::RGB(170, 210, 255); }
inline Color StatusBar() { return Color::RGB(35, 45, 65); }

}  // namespace tgdb::theme
