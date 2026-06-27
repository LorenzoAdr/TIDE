# tgdb

Terminal IDE for C++ with an integrated Visual Studio–style debugger. Built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI), connected to GDB through the [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) (`gdb -i=dap`).

Launch with no arguments to open a workspace and edit code. Press **F2** to start debugging; the UI switches to source view with breakpoints, watches, and a GDB console.

## Quick start

```bash
./tools/compile.sh
./tools/launch.sh
```

Requires Linux, CMake 3.20+, C++17, and GDB 14+ with DAP. Optional: `clangd` for outline and completion.

```bash
gdb -i=dap -ex quit   # verify DAP support
```

## Documentation

Full documentation lives in [`docs/`](docs/README.md):

| Guide | Topics |
|-------|--------|
| [User guide](docs/user-guide.md) | Workflows, CLI, IDE/debug modes, troubleshooting |
| [Keyboard shortcuts](docs/keyboard-shortcuts.md) | Complete key binding reference |
| [Architecture](docs/architecture.md) | Threads, DAP, LSP, UI structure |
| [Development](docs/development.md) | Build, project layout, contributing |

Press **F1** inside tgdb for the in-app shortcuts dialog.

## Example invocations

```bash
# IDE mode — pick workspace, edit, debug with F2
./build/tgdb
./build/tgdb --cwd ./my-project

# Direct debug launch
./build/tgdb ./build/hello
./build/tgdb --cwd ./project ./build/hello --args foo bar

# Attach
./build/tgdb --attach 12345 ./build/hello
./build/tgdb --target localhost:1234 ./build/hello
```

## Layout at a glance

| Region | IDE mode | Debug mode |
|--------|----------|------------|
| Left | File explorer | File explorer |
| Center | Editor | Source view |
| Right | Outline / Search | Outline + watches / stack |
| Bottom | Terminal | GDB console |

See the [user guide](docs/user-guide.md) for details on panels, shortcuts, and workflows.
