```
████████╗██╗   ██╗██╗██████╗ ███████╗
╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝
   ██║   ██║   ██║██║██║  ██║█████╗
   ██║   ██║   ██║██║██║  ██║██╔══╝
   ██║   ╚██████╔╝██║██████╔╝███████╗
   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝
```

# tuide

**A full IDE in the terminal** — multi-language indexing via [LSP](https://microsoft.github.io/language-server-protocol/), debugging via [DAP](https://microsoft.github.io/debug-adapter-protocol/), and a complete mouse-and-keyboard UI built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI).

tuide sits between ultra-light terminal editors (Neovim, Helix) and full graphical IDEs (VS Code): you keep the speed and footprint of a TUI — typically **20–30 MB of RAM** and very low CPU use — while still getting a comfortable, complete interface for everyday work. Setup is meant to stay out of the way: install and configure without deep tooling knowledge.

A **fully autonomous embedded terminal** runs inside the IDE (real interactive shell over a PTY), so you can build, run tools, and keep a working session without leaving the editor — including while debugging.

Beyond editing and debugging, tuide includes practical forensic tooling for native binaries:

- **Symbol forensics (`nm`)** — inspect compiled libraries and objects (`.o`, `.a`, `.so`), jump from symbols straight into source, and filter for problematic cases such as **undefined** references.
- **Core dump forensics** — post-mortem analysis of crashes, with an optional embedded [Core Analyzer](https://github.com/yanqi27/core_analyzer) build and a simple UI for heap searches and related CA commands (`obj`, `ref`, `heap`, …).

Day-to-day version control is covered too: a **simple Git interface** (status, commit, branches, push/pull) without leaving the IDE.

Prefer a self-contained binary? Build one that embeds the LSP and DAP packages you need as a blob — few system dependencies, almost autonomous. The same bundling path can include GDB with Core Analyzer.

Example: a C++ developer can ship a single binary with **clangd**, **bash-language-server**, **TexLab**, **neocmakelsp**, and **lua-language-server** already inside — and optionally Core Analyzer for crash forensics.

Launch with no arguments to open a workspace and edit. Press **F2** to start debugging or load a core; **F4** focuses the terminal; **F5** opens Git.

## Languages

tuide is a multi-language terminal IDE. Tree-sitter powers syntax highlighting and outline for all supported languages; language servers enrich completion, diagnostics, and navigation when available:

| Language | LSP (optional) | Debug |
|----------|----------------|-------|
| C / C++ | clangd | GDB (`gdb -i=dap`) |
| Python | basedpyright | debugpy |
| Shell | bash-language-server | bash-debug |
| LaTeX | TexLab | — |
| Rust | rust-analyzer | GDB |
| Go | gopls | GDB |
| Zig | zls | GDB |
| Fortran | fortls | GDB |
| Lua | lua-language-server | — |
| JavaScript / TypeScript | typescript-language-server | — |
| CMake | neocmakelsp | — |
| Make | make-ls | — |

Build with `./tools/compile.sh` to embed any of these tools in the binary, or use system installs on `PATH`.

## Quick start

```bash
./tools/compile.sh
./tools/launch.sh
```

Requires Linux, CMake 3.20+, C++17. Optional: GDB 14+ with DAP for native debugging. Sample programs live under `examples/` (`hello.cpp`, `hello.py`, `hello.rs`, …).

```bash
gdb -i=dap -ex quit   # verify DAP support
```

## Documentation

Full documentation lives in [`docs/`](docs/README.md):

| Guide | Topics |
|-------|--------|
| [User guide](docs/user-guide.md) | Workflows, CLI, languages, IDE/debug modes, troubleshooting |
| [Keyboard shortcuts](docs/keyboard-shortcuts.md) | Complete key binding reference |
| [Architecture](docs/architecture.md) | Threads, DAP, multi-LSP, UI structure |
| [Development](docs/development.md) | Build, project layout, contributing |

Press **F1** inside tuide for the in-app shortcuts dialog.

## Example invocations

```bash
# IDE mode — pick workspace, edit, debug with F2
./build/tuide
./build/tuide --cwd ./my-project

# Direct debug launch
./build/tuide ./build/hello
./build/tuide --cwd ./project ./build/hello --args foo bar

# Attach
./build/tuide --attach 12345 ./build/hello
./build/tuide --target localhost:1234 ./build/hello
```

## Layout at a glance

| Region | IDE mode | Debug mode |
|--------|----------|------------|
| Left | File explorer | File explorer |
| Center | Editor | Source view |
| Right | Outline / Search | Outline + watches / stack |
| Bottom | Terminal | Terminal + debug console |

See the [user guide](docs/user-guide.md) for details on panels, shortcuts, and workflows.

## License

Copyright © 2026 Lorenzo Arias del Real ([lorenzo.adr@proton.me](mailto:lorenzo.adr@proton.me))

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE).
