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
┌───────────────┐  ┌────────────────┐  ┌──────────────────────────┐
│  DAP thread   │  │ Indexer threads│  │  Language servers (LSP)  │
│  DapBackend   │  │ WorkspaceIndexer│  │  LspClient × N (stdio)   │
│  cppdap ↔     │  │ SymbolIndexer  │  │  clangd / basedpyright / │
│  GDB|debugpy  │  └────────────────┘  │  rust-analyzer / gopls…  │
└───────────────┘                      └──────────────────────────┘
```

All UI state lives on a single FTXUI thread. Background workers communicate through thread-safe queues or callbacks posted back to the UI loop via `Event::Custom`.

Language servers are described by `LanguageServerSpec` and started lazily per language (e.g. clangd for C/C++, basedpyright for Python, rust-analyzer for Rust, gopls for Go, …). Debug adapters use `DebugAdapterSpec` / `IDebugAdapterProcess` (`gdb -i=dap` for native binaries, `python -m debugpy.adapter` for `.py`, bash-debug for shell scripts).

## Application modes

```cpp
enum class AppMode { kNormal, kDebug };
```

| Mode | Center panel | Bottom panel | Shell |
|------|--------------|--------------|-------|
| `kNormal` | `EditorPanel` | Integrated terminal (`ShellSession`) | Running |
| `kDebug` | `SourcePanel` | Tabbed: Terminal + GDB console | Running |

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

`LspSymbolProvider` implements `ISymbolProvider` and can host **multiple** `LspClient` instances (one per language server):

1. Resolves a `LanguageServerSpec` for the buffer language (`language_server_id_for_language`)
2. Spawns the server lazily (`CLANGD_PATH` / `PATH` / bundled blob)
3. Sends `textDocument/didOpen`, `didChange`, `didClose` for editor buffers
4. Queries `documentSymbol`, completion, hover, semantic tokens, go-to-definition
5. Receives `textDocument/publishDiagnostics` for live errors/warnings

Supported server ids include clangd, basedpyright, bash-language-server, texlab, rust-analyzer, gopls, zls, fortls, lua-language-server, typescript-language-server, neocmakelsp, and make-ls.

If no LSP server is available for a file, `TreeSitterSymbolProvider` provides immediate syntactic symbols, highlighting, scopes, and local completion from the language grammar.

### Tree-sitter document cache

`TreeSitterService` owns a `TreeSitterDocumentCache` that parses off the UI thread (`ts-parse` worker). Grammars cover C/C++, Python, Bash, LaTeX, Rust, Go, Zig, Fortran, Lua, JavaScript/TypeScript, CMake, and Make:

| Piece | Role |
|-------|------|
| `request_prepare` | Coalesces edits per path; debounces parse jobs by `kTreeSitterParseDebounceMs` (200 ms) |
| `single_edit_between` + `ts_tree_edit` | Applies one contiguous edit when the buffer change is a single insertion/replacement |
| `run_prepare` | Full fallback parse when incremental edit detection fails; schedules a follow-up job if the source changed mid-parse |
| `DocumentEntry` | Cached source, `TSTree`, line highlights, symbols, revision counter |
| `queries/locals.scm` | Scopes (`@local.scope`) and definitions (`@local.definition`) for breadcrumbs and local completion |

Bracket matching, `cursor_in_code`, quote/comment text objects, and local completion require a parsed AST (`parse_ready`). There is no regex/scan fallback.

`tree_sitter_blocks.cpp` collects bracket tokens from the AST (excluding comments/strings) and pairs them with a stack. `tree_sitter_locals.cpp` runs the locals query for scope chains and visible definitions at the cursor.

## Indexing

| Component | Role |
|-----------|------|
| `WorkspaceIndexer` | File list via `rg --files`, inotify watcher; powers file picker and search |
| `SymbolWorkspaceIndexer` | Tree-sitter workspace symbol index for `Ctrl+O` without LSP |
| `index_rules` | Skip rules for dirs (`build`, `.git`, …) and file extensions |

## Integrated terminal

In `kNormal` mode the bottom panel hosts a real interactive shell (bash) over a PTY. In `kDebug` mode the same panel offers **Terminal** and **GDB** tabs; the shell keeps running while you debug.

```
┌──────────────────────────────────────────────────────────────┐
│  UI thread (FTXUI)                                           │
│  Application::Custom tick ──► ConsolePanel::terminal_tick    │
│       │                              │                       │
│       │                              ▼                       │
│       │                    refresh_terminal_view()           │
│       │                              │                       │
│       ▼                              ▼                       │
│  ShellSession ◄── keystrokes ── event_to_pty_bytes()         │
│       │                                                      │
│  output_chunks_ (ThreadSafeQueue)                            │
└───────┼──────────────────────────────────────────────────────┘
        │ reader thread
        ▼
   forkpty ──► bash (cwd = workspace root)
```

### `ShellSession`

- Spawns `bash` via `forkpty` in the workspace directory (Linux only).
- A background **reader thread** reads PTY output and enqueues byte chunks in `output_chunks_`; it never touches FTXUI or the screen buffer directly.
- On the UI thread, `drain_output_bytes()` dequeues chunks and feeds them into `RawPtyScreen`.
- `resize()` syncs terminal dimensions to the panel via `TIOCSWINSZ`.
- `write_raw()` / `send_interrupt()` forward input to the PTY master fd.

### `RawPtyScreen`

Lightweight in-process screen buffer (no external terminal library). Parses enough of the PTY byte stream for interactive bash:

- Cursor motion: `\n`, `\r`, backspace, clear line / clear to EOL
- ANSI CSI sequences (cursor positioning, erase)
- SGR color attributes (foreground/background RGB and common indexed colors)

Exposes `text()` and `styled_rows()` for FTXUI rendering. Kept on the UI thread only; guarded by `terminal_mutex_` inside `ShellSession`.

### `ConsolePanel`

- **Autostart**: when a workspace is loaded, `Application::request_terminal_autostart()` sets `console_visible` and `terminal_start_requested`. The panel calls `ShellSession::request_start()` on the next tick.
- **Tabs (debug mode)**: the bottom panel shows **Terminal** and **GDB** tabs. Entering debug (attach/launch) switches to the GDB tab automatically; the shell keeps running in the background. F4 focuses the Terminal tab.
- **Tick**: registered as `MainLayoutState::terminal_tick_callback` and invoked from the root `CatchEvent` on every `Event::Custom` (FTXUI does not deliver `Custom` to unfocused components).
- **Refresh**: drains the output queue, updates cached styled rows, and re-renders.
- **Render**: builds a `vbox` of `hbox` spans — FTXUI `text()` ignores `\n`, so each terminal row is a separate element with per-span colors.
- **Input**: when focus is `FocusRegion::Terminal` and the Terminal tab is active, keystrokes are encoded by `event_to_pty_bytes()` (`pty_input.cpp`) and written to the PTY. F4 focuses the Terminal tab.

In debug mode the GDB tab renders `DebugModel::console_output` with a GDB command input line (DAP evaluate). The Terminal tab shows the same embedded shell as in normal mode.

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
| `src/symbols/` | Symbol providers (LSP + Tree-sitter) |
| `src/parser/` | Tree-sitter multi-language parsing (highlight, symbols, scopes, blocks) |
| `src/indexer/` | Workspace and symbol indexing |
| `src/terminal/` | PTY shell session, `RawPtyScreen`, PTY key encoding |
| `src/search/` | Workspace text search |
| `src/util/` | Highlighting, paths, crash handler |

## Dependencies

Fetched via CMake `FetchContent` (see `cmake/Dependencies.cmake`):

| Library | Use |
|---------|-----|
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) v6.1.9 | Terminal UI |
| [cppdap](https://github.com/google/cppdap) | DAP client |
| [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 | JSON parsing |

Runtime: optional language servers (LSP) and debug adapters (GDB / debugpy / bash-debug).

## Planned: panel invalidation policy

FTXUI always performs a full `Draw`. To keep wakes cheap and predictable, a declarative **panel dirty + optional per-panel Hz cap** system is planned (status **label** capped at 2 Hz; Braille busy-strip spinner via ANSI without wakes; open-file / jump / tree-sitter → outline cross-invalidations made explicit). Status chrome keeps focus + toolbar buttons and drops the app name. See [Panel invalidation and wake system plan](plans/panel-invalidation-wake-system.md).

## See also

- [Development guide](development.md)
- [User guide](user-guide.md)
- [Panel invalidation plan](plans/panel-invalidation-wake-system.md)
