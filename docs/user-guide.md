# User guide

## Overview

**tuide** is a multi-language terminal IDE with an integrated debugger. It combines:

- A multi-panel editing environment (explorer, editor, outline, search, integrated shell)
- Tree-sitter syntax highlighting and outline for many languages
- Optional language servers (LSP) for completion, diagnostics, and navigation
- A Visual Studio–style debugger over DAP (GDB, debugpy, bash-debug, …)

Two application modes share the same layout; only the center and bottom panels change:

| Mode | Center panel | Bottom panel |
|------|--------------|--------------|
| **IDE** (default) | Text editor | Integrated terminal (shell) |
| **Debug** | Source view with PC and breakpoints | Terminal + debug console tabs |

Press **F2** to open the debug connection wizard from IDE mode. When a debug session is active, press **F2** again or use the stop control in the debug panel to return to the editor.

## Languages

| Language | Extensions (examples) | LSP | Debug adapter |
|----------|----------------------|-----|---------------|
| C / C++ | `.c`, `.h`, `.cpp`, `.hpp`, … | clangd | GDB (`gdb -i=dap`) |
| Python | `.py` | basedpyright | debugpy |
| Shell | `.sh`, `.bash` | bash-language-server | bash-debug |
| LaTeX | `.tex`, `.sty`, `.cls` | TexLab | — |
| Rust | `.rs` | rust-analyzer | GDB |
| Go | `.go` | gopls | GDB |
| Zig | `.zig` | zls | GDB |
| Fortran | `.f90`, `.f`, … | fortls | GDB |
| Lua | `.lua` | lua-language-server | — |
| JavaScript / TypeScript | `.js`, `.ts`, `.tsx`, … | typescript-language-server | — |
| CMake | `CMakeLists.txt`, `.cmake` | neocmakelsp | — |
| Make | `Makefile`, `.mk` | make-ls | — |
| YAML | `.yaml`, `.yml` | yaml-language-server | — |

Without an LSP server, outline, highlighting, and local completion still work via Tree-sitter. Servers start lazily when you open a matching file (clangd may also start on workspace open for C/C++).

Sample files: `examples/hello.cpp`, `hello.py`, `hello.rs`, `hello.go`, `hello.zig`, `hello.lua`, `hello.f90`, `hello.ts`, `hello.sh`, `hello.tex`, `hello.yaml`.

## Requirements

### Required

- Linux
- CMake 3.20+
- C++17 compiler (`g++` or `clang++`)

GDB 14+ with DAP is required only for **native** debugging (C/C++, Rust, Go, …). Python and shell debugging use debugpy / bash-debug instead.

Verify GDB DAP support:

```bash
gdb --version
gdb -i=dap -ex quit
```

### Optional (recommended)

Language servers and debug tools can be **system installs** on `PATH` or **embedded** via `./tools/compile.sh` (bundle wizard). Common ones:

| Tool | Role | Env override |
|------|------|--------------|
| clangd | C/C++ LSP | `CLANGD_PATH` |
| basedpyright | Python LSP | `BASEDPYRIGHT_PATH` |
| bash-language-server | Shell LSP | `BASH_LANGUAGE_SERVER_PATH` |
| texlab | LaTeX LSP | `TEXLAB_PATH` |
| rust-analyzer | Rust LSP | (PATH / bundled) |
| gopls | Go LSP | (PATH / bundled) |
| zls | Zig LSP | (PATH / bundled) |
| fortls | Fortran LSP | (PATH / bundled) |
| lua-language-server | Lua LSP | (PATH / bundled) |
| typescript-language-server | JS/TS LSP | (PATH / bundled) |
| neocmakelsp | CMake LSP | (PATH / bundled) |
| make-ls | Makefile LSP | (PATH / bundled) |
| yaml-language-server | YAML LSP | (PATH / bundled) |
| gdb | Native DAP | `GDB_PATH` |
| debugpy | Python DAP | `DEBUGPY_PYTHON` / `PYTHON` |

For **C/C++**, also provide `compile_commands.json` in the workspace root or under `build/` so clangd gets accurate include paths.

For **Makefile-only** projects, tuide detects build environments automatically (host variants, env scripts, Docker) and generates `compile_commands.json` in `.tuide/environments/` using `bear`, `compiledb`, or a built-in compiler wrapper. LSP refreshes when the active build environment changes.

When built with embedded tools, open **F10 → Configuración** to force bundled clangd/gdb (or set `TUIDE_FORCE_BUNDLED_CLANGD=1|0`, `TUIDE_FORCE_BUNDLED_GDB=1|0`, and similar for other blobs).

Portable full pack for older hosts: `./tools/build-portable.sh` (glibc ~2.31 on Ubuntu 20.04; `--bionic` for ~2.27). Embedded clangd needs **glibc ≥ 2.18**.

### Cargar core dumps

Press **F2** and choose **Cargar core**. Select the executable (with debug symbols) and the core file, then pick:

- **GDB post-mortem** — classic stack, variables, and GDB console (always available when loading cores).
- **Core Analyzer** — adds a **CoreAn** tab with CA commands (`obj`, `ref`, `heap`) and a class search panel. Only available if tuide was built with gdb + Core Analyzer support.

CLI: `tuide --core /path/to/core ./build/app` or add `--core-analyzer` for the Core Analyzer UI.

Requires debug info (`-g`) on the binary and shared libraries. Continue/step are disabled in post-mortem mode.

### Interface colors (per workspace)

Open **F10 → Configuración → Colores de interfaz** (with a workspace open) to pick a preset or tune individual colors. Settings are saved in `.tuide/config.json` under `ui_colors_preset` and `ui_colors`.

| Preset | Use case |
|--------|----------|
| **Oscuro clásico** | Default dark theme |
| **Oscuro suave** | WSL, MobaXterm, terminals that crush dark grays to black |
| **Claro clásico** | Default light theme |
| **Claro papel** | Light theme with warm, less glaring backgrounds |

Customizable roles: panel background, code background, general text, titles/active tabs, folders/outline, files/inactive tabs. **Syntax highlighting colors from Tree-sitter / LSP are unchanged.**

In the colors subpanel: **p** or **Enter** on the preset row cycles presets; **Enter** on a color row opens a visual palette (↑↓←→ to pick, Enter to confirm).

### Iconos en la interfaz

El outline, el autocompletado y el explorador de archivos muestran iconos por tipo de símbolo o archivo (método, clase, `.cpp`, `CMakeLists.txt`, etc.). Con una **Nerd Font** en la terminal se usan glifos visuales; sin ella, cada icono tiene un fallback ASCII (`M`, `++`, `cm`, `>` para carpetas, etc.).

En **F10 → Configuración → General**, la opción **Iconos Nerd Font** cicla entre:

| Modo | Comportamiento |
|------|----------------|
| **Auto** | ASCII por defecto; iconos Nerd solo si la terminal suele tener Nerd Font (kitty, wezterm, alacritty, foot, ghostty, etc.) o si defines `TUIDE_NERD_FONT=1` |
| **Siempre** | Siempre intenta glifos Nerd |
| **Nunca** | Siempre ASCII (recomendado en Konsole y terminales sin Nerd Font) |

Variable de entorno (tiene prioridad sobre el ajuste guardado): `TUIDE_ICONS=auto|always|never`. Para forzar Nerd en Auto con cualquier terminal: `TUIDE_NERD_FONT=1`.

## Installation

```bash
git clone <repo-url> tuide
cd tuide
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
| `build/tuide` | IDE and debugger |
| `build/hello` | Sample C++ program with debug symbols |

Workspace settings live under `.tuide/` (legacy `.tgdb/` is migrated automatically on open).

## Launching tuide

### IDE workflow (recommended)

Start with no arguments to pick a workspace, then edit and debug on demand:

```bash
./tools/launch.sh
./build/tuide
```

Open a workspace directly:

```bash
./tools/launch.sh --cwd /path/to/project
./build/tuide --cwd /path/to/project
```

Typical flow:

1. Select a workspace directory (or use `--cwd`)
2. Browse and edit files in the explorer / editor
3. Build from the integrated terminal (`Ctrl+T` toggles the bottom panel)
4. Press **F2** → choose **Launch** or **Attach** → pick the debug binary
5. Debug in source view; press **F2** again to stop and return to the editor

**Note:** unsaved buffers must be saved (**Ctrl+S**) before a debug session starts.

### Direct debug launch

When workspace and program are both known, tuide skips wizards and enters debug mode immediately:

```bash
./build/tuide --cwd ./project ./build/hello
./build/tuide ./build/hello --args foo bar
```

### UDP packet monitor (Launch only)

When launching a debug session via **F2 → Launch**, press **m** on the arguments step to enable the packet monitor. tuide injects `libtuide_pkt.so` via `LD_PRELOAD` and shows a **Paquetes** tab in the bottom panel.

- **r** — start/stop recording (filtered packets only)
- **s** — save recording to `~/.cache/tuide/captures/`
- **p** — cycle JSON protocol (from `.tuide/protocols/` or `examples/protocols/`)
- **v** — cycle packet-type filter (when the protocol defines a discriminator)
- **j/k** — select a packet to inspect decoded fields

Example with the bundled UDP demo (`packet_monitor_demo` simulates app + peripheral with IN/OUT traffic):

```bash
cmake --build build --target packet_monitor_demo tuide_pkt_preload
./build/tuide --cwd . ./build/packet_monitor_demo
# F2 → Launch → packet_monitor_demo → m (enable monitor) → Continue
# Tab Paquetes → p (hello_sensor.json) → r (record)
```

Protocol definitions live in `examples/protocols/*.json` (or `<workspace>/.tuide/protocols/`).

### Attach to a running process

```bash
# Local PID (may require ptrace permissions — see Troubleshooting)
./build/tuide --attach 12345 ./build/hello

# Remote gdbserver
./build/tuide --target localhost:1234 ./build/hello
```

`<program>` is always the debug binary built with symbols (`-g`), even when attaching. GDB uses it to resolve source locations and types.

## Command-line options

```
tuide [options] [program]

  --cwd <dir>              Workspace root directory
  --args <a>...            Program arguments (everything after --args)
  --attach <pid>           Attach to a local process
  --target <host:port>     Attach to a remote gdbserver
  -h, --help               Show help
```

| Invocation | Behaviour |
|------------|-----------|
| `tuide` | Workspace wizard → IDE mode |
| `tuide --cwd <dir>` | IDE mode with fixed workspace |
| `tuide <program>` | Auto debug (launch) |
| `tuide --attach <pid> <program>` | Auto debug (attach) |

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
| Editor | Multi-language highlighting (Tree-sitter), undo, find/replace, go-to-line, multi-cursor |
| Outline | Symbols in the current file (**F8**) — Tree-sitter and/or LSP |
| Workspace search | Text search across the project with include/exclude filters (**F7**) |
| Quick open | Fuzzy file picker (**Ctrl+P**) |
| Go to symbol | Workspace-wide symbol search (**Ctrl+O**) |
| Completion | LSP when a language server is available (**Ctrl+.** / **Ctrl+Space** / **F6**) |
| Problems | Live LSP diagnostics (**F9**); gutter markers `!` / `W` |
| Terminal | Embedded shell running in the workspace directory |

### Debug mode features

| Feature | Description |
|---------|-------------|
| Source view | Current file with execution pointer, breakpoint gutter, highlighting |
| Execution control | Continue (**F5**), step over (**F10**), step into (**F11**), step out (**Shift+F11**) |
| Breakpoints | Toggle on current line (**Ctrl+B**) or click the gutter |
| Debug sidebar | Watches, locals, call stack, breakpoint list (tabs 1–4) |
| Debug console | Adapter console (GDB / debugpy / …); `watch <expr>` adds a watch expression |

## GDB console tips

When debugging native binaries, the debug console sends input to GDB through DAP `evaluate` with `repl` context:

```text
-info locals
-exec info threads
watch my_variable
print *ptr
```

Use `-exec …` when you need explicit GDB/MI commands.

## Troubleshooting

### GDB DAP not available

Install GDB 14+ with Python support. Test with `gdb -i=dap -ex quit`. For Python-only or shell-only debugging, GDB is not required.

### Attach fails (ptrace)

On some Linux systems, attaching to processes owned by other users requires:

```bash
sudo sysctl kernel.yama.ptrace_scope=0
```

Attaching to your own processes usually works without this.

### Outline / completion empty

1. Confirm Tree-sitter covers the file type (highlighting should still work)
2. For LSP features, install the matching language server (or embed it with `./tools/compile.sh`)
3. For C/C++, ensure `compile_commands.json` exists in the workspace root or `build/`

### Problems panel empty

Diagnostics require an LSP server for that language. They update as you edit (servers debounce analysis). Only files opened in the editor receive diagnostics.

### File tree missing files

The explorer reads the local filesystem directly. Hidden directories and common build/cache folders are filtered by the indexer rules.

## See also

- [Keyboard shortcuts](keyboard-shortcuts.md)
- [Architecture](architecture.md)
