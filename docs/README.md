```
████████╗██╗   ██╗██╗██████╗ ███████╗
╚══██╔══╝██║   ██║██║██╔══██╗██╔════╝
   ██║   ██║   ██║██║██║  ██║█████╗
   ██║   ██║   ██║██║██║  ██║██╔══╝
   ██║   ╚██████╔╝██║██████╔╝███████╗
   ╚═╝    ╚═════╝ ╚═╝╚═════╝ ╚══════╝
```

# tuide documentation

Terminal IDE with multi-language editing and an integrated debugger. This folder contains the full project documentation.

**Author:** Lorenzo Arias del Real · [lorenzo.adr@proton.me](mailto:lorenzo.adr@proton.me) · [Apache License 2.0](../LICENSE)

## Contents

| Document | Description |
|----------|-------------|
| [User guide](user-guide.md) | Installation, workflows, languages, IDE and debug modes, CLI options |
| [Keyboard shortcuts](keyboard-shortcuts.md) | Complete key binding reference |
| [Architecture](architecture.md) | Threads, models, DAP/LSP integration, UI layout |
| [Development](development.md) | Building, project layout, conventions for contributors |
| [Toolpacks](toolpacks.md) | Language packs, catalog, CLI, AppImage/AppDir export |
| [Panel invalidation plan](plans/panel-invalidation-wake-system.md) | Design: FTXUI panel dirty cache + ANSI busy strip (separate systems) |

## Quick links

- **First run:** `./tools/compile.sh && ./tools/launch.sh`
- **In-app help:** press **F1**
- **Start debugging:** **F2** (connection wizard) or pass a binary on the command line
- **Sample programs:** `examples/hello.*` and `build/hello` (built with `-g -O0`)

## External references

- [FTXUI](https://github.com/ArthurSonzogni/FTXUI) — terminal UI framework
- [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) — GDB, debugpy, bash-debug
- [Language Server Protocol](https://microsoft.github.io/language-server-protocol/) — clangd, basedpyright, rust-analyzer, gopls, …
- [Tree-sitter](https://tree-sitter.github.io/tree-sitter/) — syntax highlighting and outline without LSP
