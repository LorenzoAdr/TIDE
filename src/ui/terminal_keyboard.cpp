#include "ui/terminal_keyboard.hpp"

#include <iostream>

namespace tgdb {

void enable_extended_key_reporting() {
  // xterm/VTE modifyOtherKeys: Ctrl+letter as CSI (not raw 0x01-0x1F).
  std::cout << "\033[>4;2m";
  // kitty keyboard protocol (ignored on terminals that do not support it).
  std::cout << "\033[>1u";
  // kitty: disambiguate escape codes so Alt/Ctrl combos arrive as single events.
  std::cout << "\033[>7u";
  std::cout.flush();
}

void disable_extended_key_reporting() {
  std::cout << "\033[>4;0m";
  std::cout << "\033[<u";
  std::cout << "\033[>0u";
  std::cout.flush();
}

}  // namespace tgdb
