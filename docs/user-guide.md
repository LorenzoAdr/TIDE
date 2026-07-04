# User guide

## Overview

**tgdb** (shown as **tide** in the status bar) is a terminal IDE for C++ projects. It combines:

- A multi-panel editing environment (explorer, editor, outline, search, integrated shell)
- A Visual Studio–style debugger connected to GDB via DAP

Two application modes share the same layout; only the center and bottom panels change:

| Mode | Center panel | Bottom panel |
|------|--------------|--------------|
| **IDE** (default) | Text editor | Integrated terminal (shell) |
| **Debug** | Source view with PC and breakpoints | Terminal + GDB tabs |

Press **F2** to open the debug connection wizard from IDE mode. When a debug session is active, press **F2** again or use the stop control in the debug panel to return to the editor.

## Requirements

### Required

- Linux
- CMake 3.20+
- C++17 compiler (`g++` or `clang++`)
- GDB 14+ with DAP support (Python enabled)

Verify GDB DAP support:

```bash
gdb --version
gdb -i=dap -ex quit
```

### Optional (recommended)

- **clangd** for LSP (outline, completion, diagnostics). Options:
  - Build with embedded clangd: `./tools/compile.sh` and check *Incluir clangd en el binario* (~87 MB `tgdb`; embedded clangd needs **glibc ≥ 2.18**)
  - Portable full pack for older hosts: `./tools/build-portable.sh` (targets **glibc ~2.31** on Ubuntu 20.04; use `--bionic` for ~2.27)
  - System install: `clangd` on `PATH`, or set `CLANGD_PATH`
- **gdb** for debugging (GDB 14+ with DAP). Options:
  - Build with embedded gdb: `./tools/compile.sh` and pick **gdb-static** or **gdb + Core Analyzer** in the bundle wizard
  - System install: `gdb` on `PATH` with `gdb -i=dap -ex quit` working, or set `GDB_PATH`
- **`compile_commands.json`** in the workspace root or under `build/` — gives clangd accurate include paths and enables reliable outline, completion, and go-to-symbol

For **Makefile-only** projects, tgdb detects build environments automatically (host variants, env scripts, Docker) and generates `compile_commands.json` in `.tgdb/environments/` using `bear`, `compiledb`, or a built-in compiler wrapper. No manual setup is required; LSP refreshes silently when the active build environment changes.

Without clangd, outline and completion fall back to regex-based symbol extraction from source files.

When built with embedded clangd, open **F10 → Configuración** to toggle *Forzar clangd embebido* (skip system `clangd` on `PATH`). Override at runtime with `TGDB_FORCE_BUNDLED_CLANGD=1|0`.

When built with embedded gdb, use **F10 → Configuración → Forzar gdb embebido** to prefer the bundled debugger over `gdb` on `PATH` (unless `GDB_PATH` is set). Override at runtime with `TGDB_FORCE_BUNDLED_GDB=1|0`.

### Cargar core dumps

Press **F2** and choose **Cargar core**. Select the executable (with debug symbols) and the core file, then pick:

- **GDB post-mortem** — classic stack, variables, and GDB console (always available when loading cores).
- **Core Analyzer** — adds a **CoreAn** tab with CA commands (`obj`, `ref`, `heap`) and a class search panel. Only available if tgdb was built with gdb + Core Analyzer support.

CLI: `tgdb --core /path/to/core ./build/app` or add `--core-analyzer` for the Core Analyzer UI.

Requires debug info (`-g`) on the binary and shared libraries. Continue/step are disabled in post-mortem mode.

### Interface colors (per workspace)

Open **F10 → Configuración → Colores de interfaz** (with a workspace open) to pick a preset or tune individual colors. Settings are saved in `.tgdb/config.json` under `ui_colors_preset` and `ui_colors`.

| Preset | Use case |
|--------|----------|
| **Oscuro clásico** | Default dark theme |
| **Oscuro suave** | WSL, MobaXterm, terminals that crush dark grays to black |
| **Claro clásico** | Default light theme |
| **Claro papel** | Light theme with warm, less glaring backgrounds |

Customizable roles: panel background, code background, general text, titles/active tabs, folders/outline, files/inactive tabs. **clangd syntax highlighting colors are unchanged.**

In the colors subpanel: **p** or **Enter** on the preset row cycles presets; **Enter** on a color row opens a visual palette (↑↓←→ to pick, Enter to confirm).

### Iconos en la interfaz

El outline, el autocompletado y el explorador de archivos muestran iconos por tipo de símbolo o archivo (método, clase, `.cpp`, `CMakeLists.txt`, etc.). Con una **Nerd Font** en la terminal se usan glifos visuales; sin ella, cada icono tiene un fallback ASCII (`M`, `++`, `cm`, `>` para carpetas, etc.).

En **F10 → Configuración → General**, la opción **Iconos Nerd Font** cicla entre:

| Modo | Comportamiento |
|------|----------------|
| **Auto** | ASCII por defecto; iconos Nerd solo si la terminal suele tener Nerd Font (kitty, wezterm, alacritty, foot, ghostty, etc.) o si defines `TGDB_NERD_FONT=1` |
| **Siempre** | Siempre intenta glifos Nerd |
| **Nunca** | Siempre ASCII (recomendado en Konsole y terminales sin Nerd Font) |

Variable de entorno (tiene prioridad sobre el ajuste guardado): `TGDB_ICONS=auto|always|never`. Para forzar Nerd en Auto con cualquier terminal: `TGDB_NERD_FONT=1`.

## Installation

```bash
git clone <repo-url> tgdb
cd tgdb
cmake -S . -B build
cmake --build build
```

Or use the helper script:

```bash
./tools/compile.sh
```

Build outputs:

| Binary | Purpose |
|--------|---------|
| `build/tgdb` | IDE and debugger |
| `build/hello` | Sample program with debug symbols |

## Launching tgdb

### IDE workflow (recommended)

Start with no arguments to pick a workspace, then edit and debug on demand:

```bash
./tools/launch.sh
./build/tgdb
```

Open a workspace directly:

```bash
./tools/launch.sh --cwd /path/to/project
./build/tgdb --cwd /path/to/project
```

Typical flow:

1. Select a workspace directory (or use `--cwd`)
2. Browse and edit files in the explorer / editor
3. Build from the integrated terminal (`Ctrl+T` toggles the bottom panel)
4. Press **F2** → choose **Launch** or **Attach** → pick the debug binary
5. Debug in source view; press **F2** again to stop and return to the editor

**Note:** unsaved buffers must be saved (**Ctrl+S**) before a debug session starts.

### Direct debug launch

When workspace and program are both known, tgdb skips wizards and enters debug mode immediately:

```bash
./build/tgdb --cwd ./project ./build/hello
./build/tgdb ./build/hello --args foo bar
```

### Attach to a running process

```bash
# Local PID (may require ptrace permissions — see Troubleshooting)
./build/tgdb --attach 12345 ./build/hello

# Remote gdbserver
./build/tgdb --target localhost:1234 ./build/hello
```

`<program>` is always the debug binary built with symbols (`-g`), even when attaching. GDB uses it to resolve source locations and types.

## Command-line options

```
tgdb [options] [program]

  --cwd <dir>              Workspace root directory
  --args <a>...            Program arguments (everything after --args)
  --attach <pid>           Attach to a local process
  --target <host:port>     Attach to a remote gdbserver
  -h, --help               Show help
```

| Invocation | Behaviour |
|------------|-----------|
| `tgdb` | Workspace wizard → IDE mode |
| `tgdb --cwd <dir>` | IDE mode with fixed workspace |
| `tgdb <program>` | Auto debug (launch) |
| `tgdb --attach <pid> <program>` | Auto debug (attach) |

## UI layout

```
┌─────────────┬──────────────────────────┬─────────────┐
│  Explorer   │   Editor / Source view   │ Outline /   │
│  (files)    │                          │ Search /    │
│             │                          │ Debug tabs  │
├─────────────┴──────────────────────────┴─────────────┤
│              Terminal / GDB console                  │
├──────────────────────────────────────────────────────┤
│  Status bar (focus region, message, shortcut hints)  │
└──────────────────────────────────────────────────────┘
```

- **Drag splitters** to resize the left column, right column, and bottom panel
- **Ctrl+T** — show/hide the bottom panel
- **Alt+←/→** — move focus between explorer, editor, and right panel
- **Alt+↓** — move focus to the terminal

### IDE mode features

| Feature | Description |
|---------|-------------|
| File explorer | Workspace tree; skips common dirs (`build`, `.git`, `node_modules`, …) |
| Editor | Syntax highlighting, undo, find/replace, go-to-line, multi-cursor selection |
| Outline | Symbols in the current file (**F8**) |
| Workspace search | Text search across the project with include/exclude filters (**F7**) |
| Quick open | Fuzzy file picker (**Ctrl+P**) |
| Go to symbol | Workspace-wide symbol search (**Ctrl+O**) |
| Completion | LSP completion when clangd is available (**Ctrl+.** / **Ctrl+Space** / **F6**) |
| Problems | Live clangd diagnostics (**F9** or **Problemas** button in editor bar); gutter markers `!` / `W` |
| Terminal | Embedded shell running in the workspace directory |

### Debug mode features

| Feature | Description |
|---------|-------------|
| Source view | Current file with execution pointer, breakpoint gutter, highlighting |
| Execution control | Continue (**F5**), step over (**F10**), step into (**F11**), step out (**Shift+F11**) |
| Breakpoints | Toggle on current line (**Ctrl+B**) or click the gutter |
| Debug sidebar | Watches, locals, call stack, breakpoint list (tabs 1–4) |
| GDB console | Native GDB commands; `watch <expr>` adds a watch expression |

## GDB console tips

The debug console sends input to GDB through DAP `evaluate` with `repl` context:

```text
-info locals
-exec info threads
watch my_variable
print *ptr
```

Use `-exec …` when you need explicit GDB/MI commands.

## Troubleshooting

### GDB DAP not available

Install GDB 14+ with Python support. Test with `gdb -i=dap -ex quit`.

### Attach fails (ptrace)

On some Linux systems, attaching to processes owned by other users requires:

```bash
sudo sysctl kernel.yama.ptrace_scope=0
```

Attaching to your own processes usually works without this.

### Outline / completion empty

1. Ensure `clangd` is installed and on `PATH` (or set `CLANGD_PATH`)
2. Ensure `compile_commands.json` exists in the workspace root or `build/`

### Problems panel empty

Diagnostics require clangd. They update as you edit (clangd debounces analysis). Only files opened in the editor receive diagnostics. Linker errors and unopened translation units may not appear until those files are opened.

### File tree missing files

The explorer reads the local filesystem directly. DAP does not provide a workspace file list. Hidden directories and common build/cache folders are filtered by the indexer rules.

## See also

- [Keyboard shortcuts](keyboard-shortcuts.md)
- [Architecture](architecture.md)
