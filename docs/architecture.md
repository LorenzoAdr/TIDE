# Architecture

## High-level overview

```
┌─────────────────────────────────────────────────────────────┐
│                     UI thread (FTXUI)                       │
│  Application ── MainLayout ── panels ── modals              │
│       │              │                                      │
│  DebugModel    WorkspaceModel    FocusManager               │
└───────┬──────────────┬────────────────────────────────────┘
        │              │
        │  UiCommand   │  file events
        ▼              ▼
┌───────────────┐  ┌────────────────┐  ┌─────────────────────┐
│  DAP thread   │  │ Indexer threads│  │  clangd (optional)  │
│  DapBackend   │  │ WorkspaceIndexer│  │  LspClient          │
│  cppdap ↔ GDB │  │ SymbolIndexer  │  │  LSP over stdio     │
└───────┬───────┘  └────────────────┘  └─────────────────────┘
        │
        ▼
   gdb -i=dap
```

All UI state lives on a single FTXUI thread. Background workers communicate through thread-safe queues or callbacks posted back to the UI loop via `Event::Custom`.

## Application modes

```cpp
enum class AppMode { kNormal, kDebug };
```

| Mode | Center panel | Bottom panel | Shell |
|------|--------------|--------------|-------|
| `kNormal` | `EditorPanel` | Integrated terminal (`ShellSession`) | Running |
| `kDebug` | `SourcePanel` | GDB console | Stopped |

`ModeLayout` switches the center panel without destroying layout geometry. The right panel adds a watches/stack section below the outline in debug mode.

## Core models

### `DebugModel`

Shared debug session state: active file/line, stack frames, locals, watches, breakpoints, console output, session status. Mutated only on the UI thread.

### `WorkspaceModel`

Editor state: workspace root, active file, `EditorBuffer` (lines, cursors, scroll). Handles load/save.

### `AppConfig`

CLI and wizard configuration: program path, workspace root, attach PID/target, launch arguments.

## Debug backend (DAP)

### Interface

`IDebugBackend` defines the contract:

- `start()` / `stop()` — lifecycle
- `submit(UiCommand)` — commands from UI

Commands (`UiCommandKind`): connect, launch, attach, continue, pause, step, evaluate, breakpoints, stack/variable refresh, watches, disconnect, quit.

Events (`DebugEventKind`): session ready, output, stopped, continued, stack/variables updated, evaluate result, errors.

### `DapBackend`

Runs on a dedicated worker thread:

1. Spawns `gdb --interpreter=dap` via `GdbProcess`
2. Creates a `cppdap::Session` over stdin/stdout
3. Drains `UiCommand` queue, sends DAP requests
4. Handles DAP events and pushes `DebugEvent` to the UI queue

The UI thread drains events in `Application::drain_events()` and updates `DebugModel`.

### Breakpoint sync

Breakpoints are tracked per normalized file path in `DebugModel`. When the user toggles a breakpoint, the UI sends `kSetBreakpoints`. The backend may pause the inferior to sync breakpoints safely before resuming.

## LSP integration

`LspSymbolProvider` implements `ISymbolProvider`:

1. On workspace open, tries to spawn `clangd` (`CLANGD_PATH` or `PATH`)
2. Sends `textDocument/didOpen`, `didChange`, `didClose` for editor buffers
3. Queries `documentSymbol` and completion

If clangd is unavailable, `RegexSymbolProvider` extracts symbols with regex heuristics.

## Indexing

| Component | Role |
|-----------|------|
| `WorkspaceIndexer` | Background scan of source files; powers file picker and search |
| `SymbolWorkspaceIndexer` | Regex-based workspace symbol index for `Ctrl+O` without LSP |
| `WorkspaceWatcher` | Filesystem watcher; triggers re-index on changes |
| `index_rules` | Skip rules for dirs (`build`, `.git`, …) and file extensions |

## Terminal

## Integrated terminal

`ShellSession` opens a PTY (`forkpty`) in the workspace directory and runs an interactive bash. Raw PTY output is fed into **libvterm** (`TerminalEmulator`), which maintains a full VT100/xterm screen grid (colors, cursor, scrollback). `ConsolePanel` renders that grid with FTXUI and forwards keystrokes to the PTY via `event_to_pty_bytes()`. Terminal size is synced to the panel dimensions with `TIOCSWINSZ`.

In debug mode the same panel renders GDB console output and accepts GDB commands.

## UI composition

```
MakeMainLayout
├── FileTreePanel          (left)
├── ModeLayout             (center)
│   ├── EditorPanel        (kNormal)
│   └── SourcePanel        (kDebug)
├── RightPanelLayout       (right)
│   ├── RightSidebarPanel  (outline + search tabs)
│   └── WatchesPanel       (kDebug only)
└── ConsolePanel           (bottom)
```

Overlays (stacked on top):

- File picker (`Ctrl+P`)
- Symbol picker (`Ctrl+O`)
- Connection wizard (`F2`)
- Workspace wizard (`F3`)
- Shortcuts modal (`F1`)
- Quit confirmation (`Ctrl+Q`)

## Event flow

1. FTXUI delivers keyboard/mouse events to `Application::run()` root `CatchEvent`
2. Global shortcuts handled first (F1, F2, debug keys, focus shortcuts)
3. Panel-specific handlers via `MainLayoutState` callbacks (editor, console, search)
4. `Event::Custom` triggers queue drain, index updates, focus sync

## Key source directories

| Path | Contents |
|------|----------|
| `src/app/` | `Application`, models, config |
| `src/ui/` | FTXUI panels, layout, modals, theme |
| `src/backend/` | DAP backend, command/event types |
| `src/dap/` | GDB process launch, protocol helpers |
| `src/editor/` | Buffer, text ops, undo, find, render |
| `src/lsp/` | LSP transport and client |
| `src/symbols/` | Symbol providers (LSP + regex) |
| `src/indexer/` | Workspace and symbol indexing |
| `src/terminal/` | PTY shell session, libvterm emulator, PTY key encoding |
| `src/search/` | Workspace text search |
| `src/util/` | Highlighting, paths, crash handler |

## Dependencies

Fetched via CMake `FetchContent` (see `cmake/Dependencies.cmake`):

| Library | Use |
|---------|-----|
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v6.1.9 | Terminal UI |
| [cppdap](https://github.com/google/cppdap) | DAP client |
| [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 | JSON parsing |

Runtime: GDB (DAP), optionally clangd (LSP).

## See also

- [Development guide](development.md)
- [User guide](user-guide.md)
