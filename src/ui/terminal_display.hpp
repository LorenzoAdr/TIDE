#pragma once

namespace tuide {

// Empuja el búfer de salida al emulador. Útil en Windows Terminal / WSL / SSH,
// donde el repintado puede quedar retrasado hasta el siguiente evento de entrada.
void nudge_terminal_repaint();

}  // namespace tuide
