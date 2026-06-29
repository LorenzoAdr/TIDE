#include "ui/shortcuts_modal.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "ui/panel.hpp"
#include "ui/theme.hpp"

namespace tgdb {

using namespace ftxui;

namespace {

constexpr int kKeyWidth = 20;
constexpr int kVisibleRows = 20;

struct ShortcutEntry {
  const char* key;
  const char* desc;
};

struct ShortcutSection {
  const char* title;
  std::vector<ShortcutEntry> entries;
};

const std::vector<ShortcutSection>& shortcut_sections() {
  static const std::vector<ShortcutSection> sections = {
      {"General",
       {
           {"F1", "Atajos de teclado (este diálogo)"},
           {"F2", "Asistente de depuración / detener sesión"},
           {"F3", "Cambiar directorio de trabajo"},
           {"F4", "Pestaña Terminal (shell)"},
           {"F7", "Panel de búsqueda en workspace"},
           {"F8", "Panel outline (símbolos del archivo)"},
           {"F9", "Mostrar / ocultar panel de problemas (clangd)"},
           {"F10", "Configuración (modo normal)"},
           {"Ctrl+P", "Abrir archivo rápido"},
           {"Ctrl+O", "Ir a símbolo"},
           {"Ctrl+T", "Mostrar / ocultar panel inferior"},
           {"Ctrl+Q", "Salir (confirmación)"},
           {"Ctrl+A", "Enfocar explorador de archivos"},
           {"Ctrl+E", "Enfocar editor"},
           {"Alt+← / →", "Mover foco izquierda / derecha (fuera del editor)"},
           {"Alt+↑ / ↓", "Mover foco arriba / a terminal"},
           {"Esc", "Quitar foco de entrada (fuera del editor)"},
       }},
      {"Editor",
       {
           {"Ctrl+S", "Guardar archivo"},
           {"Ctrl+F", "Buscar en archivo"},
           {"Ctrl+G", "Ir a línea"},
           {"Ctrl+Z", "Deshacer"},
           {"Ctrl+C", "Copiar selección"},
           {"Ctrl+V", "Pegar"},
           {"Ctrl+U", "Media página arriba (cursor + scroll)"},
           {"Ctrl+I", "Media página abajo (cursor + scroll)"},
           {"Ctrl+Backspace", "Borrar palabra anterior"},
           {"Ctrl+Delete", "Borrar palabra siguiente"},
           {"Ctrl+D", "Seleccionar siguiente coincidencia (multicursor)"},
           {"Ctrl+Shift+D", "Seleccionar siguiente coincidencia (multicursor)"},
           {"Ctrl+Shift+L", "Seleccionar todas las coincidencias"},
           {"Ctrl+.", "Completar código (LSP)"},
           {"Ctrl+Espacio", "Completar código (LSP)"},
           {"F6", "Completar código (LSP)"},
           {"Al escribir", "Autocompletado automático (identificadores)"},
           {"F12", "Ir a definición (LSP)"},
           {"Alt+← / →", "Posición anterior / siguiente del cursor"},
           {"Shift+F12", "Ir a declaración (LSP)"},
           {"Ctrl+clic", "Ir a definición (LSP)"},
           {"Ctrl+Shift+clic", "Ir a declaración (LSP)"},
           {"Tab", "Indentar (no cambia panel)"},
           {"Shift+flechas", "Extender selección"},
           {"Ctrl+flechas", "Mover por palabras"},
           {"Ctrl+Shift+↑/↓", "Selección en bloque vertical"},
       }},
      {"Depuración",
       {
           {"F5", "Continuar"},
           {"F10", "Step over"},
           {"F11", "Step into"},
           {"Shift+F11", "Step out"},
           {"Ctrl+U", "Scroll media página arriba"},
           {"Ctrl+I", "Scroll media página abajo"},
           {"Ctrl+B", "Breakpoint en línea actual"},
           {"Clic gutter", "Alternar breakpoint"},
           {"Consola GDB", "Comandos GDB o watch <expr>"},
           {"1 / 2", "Pestañas inferior: Terminal / GDB"},
       }},
      {"Panel de depuración (derecha)",
       {
           {"1–4", "Pestañas: watches / variables / stack / breakpoints"},
           {"j / k", "Navegar filas"},
           {"Enter", "Expandir variable / ir a frame"},
           {"e / =", "Editar valor de watch"},
           {"x / d", "Eliminar watch o breakpoint"},
       }},
      {"Terminal integrado",
       {
           {"Enter / clic", "Escribir en la shell"},
           {"Tab", "Enviar tabulación a la shell"},
       }},
  };
  return sections;
}

Element render_key(const std::string& key) {
  return text(key) | color(theme::Accent()) | bold | size(WIDTH, EQUAL, kKeyWidth);
}

Element render_desc(const std::string& desc) {
  return text(desc) | color(theme::Header()) | flex;
}

std::vector<Element> build_rows() {
  std::vector<Element> rows;
  for (const auto& section : shortcut_sections()) {
    rows.push_back(text(section.title) | bold | color(theme::Accent()) | underlined);
    for (const auto& entry : section.entries) {
      rows.push_back(hbox({render_key(entry.key), render_desc(entry.desc)}));
    }
    rows.push_back(separator() | color(theme::AccentDim()));
  }
  if (!rows.empty()) {
    rows.pop_back();
  }
  return rows;
}

void clamp_scroll(ShortcutsModalState* state, int total_rows) {
  if (state == nullptr) {
    return;
  }
  const int max_first = std::max(0, total_rows - kVisibleRows);
  state->first_visible = std::max(0, std::min(state->first_visible, max_first));
}

bool handle_scroll_keys(ShortcutsModalState* state, Event event, int total_rows) {
  if (state == nullptr || !state->open) {
    return false;
  }

  if (event == Event::Escape || event == Event::F1) {
    state->open = false;
    state->first_visible = 0;
    return true;
  }
  if (event == Event::ArrowDown || event == Event::Character('j')) {
    state->first_visible += 1;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::ArrowUp || event == Event::Character('k')) {
    state->first_visible -= 1;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::PageDown) {
    state->first_visible += kVisibleRows;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::PageUp) {
    state->first_visible -= kVisibleRows;
    clamp_scroll(state, total_rows);
    return true;
  }
  if (event == Event::Home) {
    state->first_visible = 0;
    return true;
  }
  if (event == Event::End) {
    state->first_visible = std::max(0, total_rows - kVisibleRows);
    return true;
  }
  return true;
}

}  // namespace

Component MakeShortcutsModalOverlay(Component main, ShortcutsModalState* state) {
  auto rows = std::make_shared<std::vector<Element>>(build_rows());

  return Renderer(
      CatchEvent(main, [state, rows](Event event) {
        if (state == nullptr || !state->open) {
          return false;
        }
        return handle_scroll_keys(state, event, static_cast<int>(rows->size()));
      }),
      [main, state, rows] {
        Element base = main->Render();
        if (state == nullptr || !state->open) {
          return base;
        }

        clamp_scroll(state, static_cast<int>(rows->size()));

        Elements visible;
        const int end = std::min(static_cast<int>(rows->size()),
                                 state->first_visible + kVisibleRows);
        for (int i = state->first_visible; i < end; ++i) {
          visible.push_back((*rows)[static_cast<std::size_t>(i)]);
        }

        const int total = static_cast<int>(rows->size());
        const bool can_scroll = total > kVisibleRows;
        std::string footer = "F1 / Esc cerrar";
        if (can_scroll) {
          footer += "  ↑↓ j/k scroll  PgUp/PgDn  Home/End";
        }

        Element dialog = ModalWindow(
            text("Atajos de teclado") | color(theme::Accent()),
            vbox({
                vbox(std::move(visible)) | flex,
                separator() | color(theme::AccentDim()),
                text(footer) | color(theme::Muted()),
            }));

        return ScreenModalOverlay(std::move(base), std::move(dialog));
      });
}

}  // namespace tgdb
