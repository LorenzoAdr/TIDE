#include "ui/terminal_display.hpp"

#include <cstdio>
#include <iostream>
#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace tgdb {

void nudge_terminal_repaint() {
  // Alternar visibilidad del cursor fuerza a muchos emuladores (p. ej. ConPTY)
  // a sincronizar el frame sin alterar el contenido visible.
  std::cout << "\033[?25l\033[?25h" << std::flush;
#if defined(__linux__) || defined(__APPLE__)
  if (FILE* out = stdout) {
    fflush(out);
    fsync(fileno(out));
  }
#endif
}

}  // namespace tgdb
