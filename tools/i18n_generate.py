#!/usr/bin/env python3
"""Generate src/i18n/strings_es.cpp and strings_en.cpp from catalog."""

from __future__ import annotations

import sys
from pathlib import Path

_tools_dir = Path(__file__).resolve().parent
if str(_tools_dir) not in sys.path:
    sys.path.insert(0, str(_tools_dir))
from i18n_catalog_extra import EXTRA_CATALOG

# (key, spanish, english)
CATALOG: list[tuple[str, str, str]] = [
    # common
    ("common.empty", "(vacío)", "(empty)"),
    ("common.no_matches", "(sin coincidencias)", "(no matches)"),
    ("common.no_results", "(sin resultados)", "(no results)"),
    ("common.no_data", "(sin datos)", "(no data)"),
    ("common.no_mappings", "(sin mapeos)", "(no mappings)"),
    ("common.no_extra_paths", "(sin rutas extra)", "(no extra paths)"),
    ("common.ellipsis", "  ...", "  ..."),
    ("common.yes", " Sí ", " Yes "),
    ("common.no", " No ", " No "),
    ("common.ok", " OK ", " OK "),
    ("common.cancel", "Cancelar", "Cancel"),
    ("common.workspace_prefix", "workspace: ", "workspace: "),
    ("common.workspace", "workspace: {0}", "workspace: {0}"),
    ("common.browser.dir_prefix", "[dir] ", "[dir] "),
    ("common.browser.file_prefix", "[file] ", "[file] "),
    ("common.browser.file_indent", "      ", "      "),
    ("common.highlight.wrap", " {0} ", " {0} "),
    ("common.truncation_suffix", "...", "..."),
    ("common.footer.confirm_esc", "Enter confirmar  Esc cancelar", "Enter confirm  Esc cancel"),
    ("common.footer.esc_close", " Esc cerrar", " Esc close"),
    ("common.action.prefix", "  ▸  ", "  ▸  "),
    ("common.none", "(ninguno)", "(none)"),
    ("common.indexing", "(indexando...)", "(indexing...)"),
    # welcome
    ("welcome.tagline", "depura y edita desde la terminal", "debug and edit from the terminal"),
    ("welcome.action.open_external", "F1   Abrir archivo suelto", "F1   Open external file"),
    ("welcome.action.start_debug", "F2   Iniciar depuración", "F2   Start debugging"),
    ("welcome.action.open_workspace", "F3   Abrir workspace", "F3   Open workspace"),
    ("welcome.shortcuts_hint", "Alt+F1 / Shift+F1 atajos de teclado", "Alt+F1 / Shift+F1 keyboard shortcuts"),
    ("welcome.author", "Lorenzo Arias del Real", "Lorenzo Arias del Real"),
    ("welcome.email", "lorenzo.adr@proton.me", "lorenzo.adr@proton.me"),
    ("welcome.license", "Apache License 2.0", "Apache License 2.0"),
    # focus
    ("focus.region.explorer", "Explorador", "Explorer"),
    ("focus.region.editor", "Editor", "Editor"),
    ("focus.region.editor_secondary", "Editor 2", "Editor 2"),
    ("focus.region.outline", "Outline", "Outline"),
    ("focus.region.terminal", "Terminal", "Terminal"),
    # theme
    ("theme.preset.dark_classic", "Oscuro clásico", "Dark classic"),
    ("theme.preset.dark_soft", "Oscuro suave", "Dark soft"),
    ("theme.preset.nord", "Nord", "Nord"),
    ("theme.preset.gruvbox_dark", "Gruvbox oscuro", "Gruvbox dark"),
    ("theme.preset.one_dark", "One Dark", "One Dark"),
    ("theme.preset.dracula", "Dracula", "Dracula"),
    ("theme.preset.monokai", "Monokai", "Monokai"),
    ("theme.preset.tokyo_night", "Tokyo Night", "Tokyo Night"),
    ("theme.preset.light_classic", "Claro clásico", "Light classic"),
    ("theme.preset.light_paper", "Claro papel", "Light paper"),
    ("theme.preset.gruvbox_light", "Gruvbox claro", "Gruvbox light"),
    ("theme.preset.solarized_light", "Solarized claro", "Solarized light"),
    ("theme.preset.custom", "Personalizado", "Custom"),
    # settings locale
    ("settings.general.ui_locale.label", "Idioma de interfaz", "Interface language"),
    ("settings.general.ui_locale.description", "Auto detecta LANG; Español o English para forzar idioma", "Auto detects LANG; Spanish or English to force language"),
    ("settings.locale.auto", "Auto", "Auto"),
    ("settings.locale.es", "Español", "Spanish"),
    ("settings.locale.en", "English", "English"),
    ("settings.locale.mode_label", "Idioma ({0})", "Language ({0})"),
    # modal shortcuts
    ("modal.shortcuts.title", "Atajos de teclado", "Keyboard shortcuts"),
    ("modal.shortcuts.footer.close", "Alt+F1 / Shift+F1 / Esc cerrar", "Alt+F1 / Shift+F1 / Esc close"),
    ("modal.shortcuts.footer.scroll", "  ↑↓ j/k scroll  PgUp/PgDn  Home/End", "  ↑↓ j/k scroll  PgUp/PgDn  Home/End"),
    # shortcut sections
    ("shortcuts.section.general", "General", "General"),
    ("shortcuts.section.editor", "Editor", "Editor"),
    ("shortcuts.section.debug", "Depuración", "Debugging"),
    ("shortcuts.section.debug_panel", "Panel de depuración (derecha)", "Debug panel (right)"),
    ("shortcuts.section.terminal", "Terminal integrado", "Integrated terminal"),
    ("shortcuts.section.performance", "Rendimiento", "Performance"),
    ("shortcuts.section.git", "Git", "Git"),
    # shortcuts — General
    ("shortcuts.general.open_external", "Abrir archivo externo (explorador desde raíz)", "Open external file (browser from root)"),
    ("shortcuts.general.open_external_here", "Abrir explorador de archivos desde la carpeta actual", "Open file browser from current folder"),
    ("shortcuts.general.keyboard_shortcuts", "Atajos de teclado (este diálogo)", "Keyboard shortcuts (this dialog)"),
    ("shortcuts.general.debug_wizard", "Asistente de depuración / detener sesión", "Debug wizard / stop session"),
    ("shortcuts.general.change_workspace", "Cambiar directorio de trabajo", "Change working directory"),
    ("shortcuts.general.terminal_tab", "Pestaña Terminal (shell)", "Terminal tab (shell)"),
    ("shortcuts.general.git_page", "Página Git (status, commit, ramas)", "Git page (status, commit, branches)"),
    ("shortcuts.general.quick_launch", "Relanzar último launch (▸ junto a Lanzar)", "Relaunch last launch (▸ next to Launch)"),
    ("shortcuts.general.search_panel", "Panel de búsqueda en workspace", "Workspace search panel"),
    ("shortcuts.general.outline_panel", "Panel outline (símbolos del archivo)", "Outline panel (file symbols)"),
    ("shortcuts.general.problems_panel", "Mostrar / ocultar panel de problemas (clangd)", "Show / hide problems panel (clangd)"),
    ("shortcuts.general.binary_symbols", "Panel de símbolos de binario (nm)", "Binary symbols panel (nm)"),
    ("shortcuts.general.settings", "Configuración (modo normal)", "Settings (normal mode)"),
    ("shortcuts.general.quick_open", "Abrir archivo rápido (Ctrl+P / Alt+P)", "Quick open file (Ctrl+P / Alt+P)"),
    ("shortcuts.general.go_to_symbol", "Ir a símbolo", "Go to symbol"),
    ("shortcuts.general.toggle_bottom_panel", "Mostrar / ocultar panel inferior", "Show / hide bottom panel"),
    ("shortcuts.general.quit", "Salir (confirmación)", "Quit (confirmation)"),
    ("shortcuts.general.focus_explorer", "Enfocar explorador de archivos", "Focus file explorer"),
    ("shortcuts.general.focus_editor", "Enfocar editor", "Focus editor"),
    ("shortcuts.general.move_focus_horizontal", "Mover foco izquierda / derecha (fuera del editor)", "Move focus left / right (outside editor)"),
    ("shortcuts.general.move_focus_vertical", "Mover foco arriba / a terminal", "Move focus up / to terminal"),
    ("shortcuts.general.unfocus_input", "Quitar foco de entrada (fuera del editor)", "Unfocus input (outside editor)"),
] + EXTRA_CATALOG


def cpp_escape(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
    )


def write_table(path: Path, fn_name: str, column: int) -> None:
    lines = [
        '#include "i18n/strings_internal.hpp"',
        "",
        "namespace tuide::i18n {",
        "",
        f"const StringTable& {fn_name}() {{",
        "  static const StringTable table = {",
    ]
    for key, es, en in CATALOG:
        value = es if column == 0 else en
        lines.append(f'      {{"{key}", "{cpp_escape(value)}"}},')
    lines.extend(["  };", "  return table;", "}", "", "}  // namespace tuide::i18n", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    write_table(root / "src/i18n/strings_es.cpp", "spanish_strings", 0)
    write_table(root / "src/i18n/strings_en.cpp", "english_strings", 1)
    print(f"Generated {len(CATALOG)} entries")
    return 0


if __name__ == "__main__":
    sys.exit(main())
