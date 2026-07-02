# Keyboard shortcuts

Press **F1** inside tgdb to open the in-app shortcuts dialog (scrollable). This page is the full reference.

## Global

| Key | Action |
|-----|--------|
| F1 | Keyboard shortcuts dialog |
| F2 | Debug connection wizard / stop debug session |
| F3 | Change workspace directory |
| F4 | Focus terminal tab (shell) |
| F7 | Open workspace search panel |
| F8 | Open outline panel |
| Ctrl+P | Quick open (file picker) |
| Ctrl+O | Go to symbol |
| Ctrl+T | Show / hide bottom panel |
| Ctrl+Q | Quit (with confirmation) |
| Ctrl+A | Focus file explorer |
| Ctrl+E | Focus editor |
| Alt+← / → | Move focus left / right |
| Alt+↑ / ↓ | Move focus up / to terminal |
| Esc | Clear input focus (outside editor) |

## Editor

| Key | Action |
|-----|--------|
| Ctrl+S | Save file |
| Ctrl+F | Find in file |
| Ctrl+G | Go to line |
| Ctrl+Z | Undo |
| Ctrl+Alt+Z / Ctrl+Shift+Z / Ctrl+Y | Redo |
| Ctrl+C | Copy selection |
| Ctrl+V | Paste |
| Ctrl+U | Half page up (moves cursor) |
| Ctrl+I | Half page down (moves cursor) |
| Ctrl+Backspace | Delete previous word |
| Ctrl+Delete | Delete next word |
| Ctrl+D / Ctrl+Alt+D / Ctrl+Shift+D | Select next match (multi-cursor) |
| Ctrl+Alt+L / Ctrl+Shift+L | Select all matches |
| Ctrl+Alt+F / Ctrl+Shift+F | Search selection in workspace |
| Ctrl+. | Code completion (LSP) |
| Ctrl+Space | Code completion (LSP) |
| F6 | Code completion (LSP) |
| Tab | Indent (does not cycle panel focus) |
| Shift+arrows | Extend selection |
| Ctrl+arrows | Move by word |
| Ctrl+Alt+↑/↓ / Ctrl+Shift+↑/↓ | Block selection (vertical) |
| Ctrl+Alt+click / Ctrl+Shift+click | Go to declaration (LSP) |

## Debug

| Key | Action |
|-----|--------|
| F5 | Continue |
| F10 | Step over |
| F11 | Step into |
| Shift+F11 | Step out |
| Ctrl+U | Scroll half page up |
| Ctrl+I | Scroll half page down |
| Ctrl+B | Toggle breakpoint on current line |
| Click gutter | Toggle breakpoint |

In the GDB console, type native GDB commands or `watch <expression>` to add a watch.

## Debug panel (right sidebar, debug mode)

| Key | Action |
|-----|--------|
| 1–4 | Switch tabs: watches / variables / stack / breakpoints |
| j / k | Navigate rows |
| Enter | Expand variable / go to stack frame |
| e / = | Edit watch value |
| x / d | Delete watch or breakpoint |

## Integrated terminal

In normal mode the bottom panel is the embedded shell. In debug mode it has two tabs:

| Tab | Content |
|-----|---------|
| Terminal | Same bash shell as in IDE mode (F4 to focus) |
| GDB | DAP console output and command input (auto-selected on attach/launch) |

Switch tabs by clicking the tab bar. With the bottom panel focused, `1` / `2` select Terminal / GDB.

| Key | Action |
|-----|--------|
| Enter / click | Focus and type in the shell |
| Tab | Send tab to the shell (does not change panel focus) |

## Focus navigation

Focus cycles through four regions: **Explorer → Editor → Right panel → Terminal**.

| Key | From | Action |
|-----|------|--------|
| Alt+← | Editor / Right panel | Move left |
| Alt+→ | Explorer / Editor | Move right |
| Alt+↓ | Any (except editor arrows) | Move to terminal |
| Alt+↑ | Terminal | Move to editor |
| F4 | Any | Jump to terminal |
| Ctrl+A | Any | Jump to explorer |
| Ctrl+E | Any | Jump to editor |

Tab never cycles between panels. In the editor it inserts a tab character; in the terminal it sends a tab to the shell.
