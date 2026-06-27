# Keyboard shortcuts

Press **F1** inside tgdb to open the in-app shortcuts dialog (scrollable). This page is the full reference.

## Global

| Key | Action |
|-----|--------|
| F1 | Keyboard shortcuts dialog |
| F2 | Debug connection wizard / stop debug session |
| F3 | Change workspace directory |
| F4 | Focus terminal |
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
| Ctrl+C | Copy selection |
| Ctrl+V | Paste |
| Ctrl+U | Half page up (moves cursor) |
| Ctrl+D | Half page down (moves cursor) |
| Ctrl+Backspace | Delete previous word |
| Ctrl+Delete | Delete next word |
| Ctrl+Shift+D | Select next match |
| Ctrl+Shift+L | Select all matches |
| Ctrl+. | Code completion (LSP) |
| Ctrl+Space | Code completion (LSP) |
| F6 | Code completion (LSP) |
| Tab | Indent (does not cycle panel focus) |
| Shift+arrows | Extend selection |
| Ctrl+arrows | Move by word |
| Ctrl+Shift+↑/↓ | Block selection (vertical) |

## Debug

| Key | Action |
|-----|--------|
| F5 | Continue |
| F10 | Step over |
| F11 | Step into |
| Shift+F11 | Step out |
| Ctrl+U | Scroll half page up |
| Ctrl+D | Scroll half page down |
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
