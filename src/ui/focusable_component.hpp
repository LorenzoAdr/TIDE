#pragma once

#include "ftxui/component/component.hpp"

namespace tuide {

// FTXUI solo entrega teclado a hijos Focusable() dentro de contenedores.
ftxui::Component WrapFocusable(ftxui::Component child);

}  // namespace tuide
