#include "ui/terminal_display.hpp"

#include <cstdio>
#include <iostream>
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace tgdb {

void nudge_terminal_repaint() {
  // Alternar visibilidad del cursor fuerza a muchos emuladores (p. ej. ConPTY, SSH)
  // a sincronizar el frame sin alterar el contenido visible.
  static constexpr char kCursorSync[] = "\033[?25l\033[?25h\033[0m";
  // DSR cursor position: strict ptys (PowerShell+WSL) often composite only after
  // stdin activity; the response unblocks the input thread like a mouse event.
  static constexpr char kCursorQuery[] = "\033[6n";

  std::cout << kCursorSync << kCursorQuery << std::flush;
#if defined(__linux__) || defined(__APPLE__)
  if (FILE* tty = std::fopen("/dev/tty", "w")) {
    std::fputs(kCursorSync, tty);
    std::fputs(kCursorQuery, tty);
    std::fflush(tty);
    fsync(fileno(tty));
    std::fclose(tty);
  }
  if (FILE* out = stdout) {
    fflush(out);
    fsync(fileno(out));
  }
#endif
}

}  // namespace tgdb
