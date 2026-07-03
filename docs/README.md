```
████████╗██╗   ██╗██╗██████╗ ███████╗
╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝
   ██║   ██║   ██║██║██║  ██║█████╗
   ██║   ██║   ██║██║██║  ██║██╔══╝
   ██║   ╚██████╔╝██║██████╔╝███████╗
   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝
```

# tgdb documentation

Terminal IDE for C++ with an integrated debugger. This folder contains the full project documentation.

**Author:** Lorenzo Arias del Real · [lorenzo.adr@proton.me](mailto:lorenzo.adr@proton.me) · [Apache License 2.0](../LICENSE)

## Contents

| Document | Description |
|----------|-------------|
| [User guide](user-guide.md) | Installation, workflows, IDE and debug modes, CLI options |
| [Keyboard shortcuts](keyboard-shortcuts.md) | Complete key binding reference |
| [Architecture](architecture.md) | Threads, models, DAP/LSP integration, UI layout |
| [Development](development.md) | Building, project layout, conventions for contributors |

## Quick links

- **First run:** `./tools/compile.sh && ./tools/launch.sh`
- **In-app help:** press **F1**
- **Start debugging:** **F2** (connection wizard) or pass a binary on the command line
- **Sample program:** `build/hello` (built with `-g -O0`)

## External references

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) — terminal UI framework
- [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) — GDB integration
- [clangd](https://clangd.llvm.org/) — optional LSP server for symbols and completion
