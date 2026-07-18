#include "ui/terminal_keyboard.hpp"

#include <iostream>

namespace tuide {

void enable_extended_key_reporting() {
  // xterm/VTE modifyOtherKeys: Ctrl+letter as CSI (not raw 0x01-0x1F).
  // Level 4: also report modifier keys pressed alone (Shift/Ctrl/Alt).
  std::cout << "\033[>4;4m";
  // Kitty keyboard protocol: disambiguate (1) + event types (2) + all keys (8).
  // Flag 8 is required for modifier-only key press/release (e.g. Konsole).
  std::cout << "\033[>11u";
  std::cout.flush();
}

void disable_extended_key_reporting() {
  std::cout << "\033[>4;0m";
  std::cout << "\033[<u";
  std::cout.flush();
}

void enable_click_drag_mouse_reporting() {
  // FTXUI TrackMouse(true) enables DEC 1003 (any-event), which reports every pointer
  // move and forces a full repaint per event. We only need clicks/wheel (1000) and
  // motion while a button is held (1002). SGR coordinates (1006) match FTXUI's parser.
  std::cout << "\033[?1003l";  // clear any-event if a prior session left it on
  std::cout << "\033[?1000h";  // VT200 click/wheel
  std::cout << "\033[?1002h";  // button-event drag tracking
  std::cout << "\033[?1006h";  // SGR extended coordinates
  std::cout.flush();
}

void disable_click_drag_mouse_reporting() {
  std::cout << "\033[?1006l";
  std::cout << "\033[?1002l";
  std::cout << "\033[?1000l";
  std::cout << "\033[?1003l";
  std::cout.flush();
}

}  // namespace tuide
