#include "ui/terminal_keyboard.hpp"

#include <iostream>

namespace tgdb {

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

}  // namespace tgdb
