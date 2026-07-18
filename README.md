```
████████╗██╗   ██╗██╗██████╗ ███████╗
╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝
   ██║   ██║   ██║██║██║  ██║█████╗
   ██║   ██║   ██║██║██║  ██║██╔══╝
   ██║   ╚██████╔╝██║██████╔╝███████╗
   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝
```

# tuide

Terminal IDE with an integrated Visual Studio–style debugger. Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI), connected to debug adapters through the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (GDB, debugpy, bash-debug, …).

Launch with no arguments to open a workspace and edit code. Press **F2** to start debugging; the UI switches to source view with breakpoints, watches, and a debug console.

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
