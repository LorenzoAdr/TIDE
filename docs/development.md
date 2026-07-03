# Development guide

## Prerequisites

Same as the [user guide](user-guide.md#requirements), plus:

- `git`
- `nproc` (optional, used by `tools/compile.sh` for parallel jobs)

## Building

### Standard build

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

CMake exports `compile_commands.json` in the build directory (`CMAKE_EXPORT_COMPILE_COMMANDS ON`). Symlink or copy it to the project root if you use clangd locally:

```bash
ln -sf build/compile_commands.json .
```

### Helper scripts

| Script | Purpose |
|--------|---------|
| `tools/compile.sh` | Interactive bundle wizard (default), configure, build |
| `tools/build-portable.sh` | Full pack build inside Docker (old glibc baseline) |
| `tools/verify-glibc.sh` | Report max GLIBC/GLIBCXX symbols and `ldd` deps |
| `tools/launch.sh` | Run `tgdb` with sensible defaults and path resolution |

`compile.sh` without arguments opens a TUI to choose embedded components (clangd). Use `-y` to skip the wizard and reuse `.bundle-config`.

```bash
./tools/compile.sh                      # TUI: choose bundles
./tools/compile.sh -y                   # reuse .bundle-config
./tools/compile.sh --bundle-clangd      # embed official clangd (Linux x86_64)
./tools/compile.sh --no-bundle-clangd   # slim binary (~53 MB)
./tools/compile.sh --bundle-gdb         # embed gdb + Core Analyzer
./tools/compile.sh --help
```

When `TGDB_BUNDLE_CLANGD=ON`, the build downloads the official [clangd/clangd](https://github.com/clangd/clangd/releases) Linux x86_64 release, strips it, compresses it, and embeds it in `tgdb` (+~35 MB compressed, ~87 MB total). At runtime the blob is extracted once to `$XDG_CACHE_HOME/tgdb/bundled/clangd-<version>/`.

When `TGDB_BUNDLE_GDB=ON`, choose the bundled gdb kind at compile time:

| `TGDB_GDB_BUNDLE_KIND` | Runtime | Core Analyzer UI |
|------------------------|---------|------------------|
| `static` | gdb-static (musl, no deps) | disabled |
| `core_analyzer` | GDB 16.3 + CA (dynamic deps) | enabled |

Use `./tools/compile.sh` (TUI wizard) or flags:

```bash
./tools/compile.sh --bundle-gdb-static -y   # portable gdb, no CA
./tools/compile.sh --bundle-gdb-ca -y       # gdb + Core Analyzer
./tools/compile.sh --no-bundle-gdb -y       # system gdb on PATH
```

Static bundle downloads [gdb-static Full](https://github.com/guyush1/gdb-static/releases). Core Analyzer bundle uses a cached tarball or `-DTGDB_BUILD_GDB_CA=ON` / `docker/Dockerfile.gdb-ca`. Runtime extraction: `$XDG_CACHE_HOME/tgdb/bundled/gdb-<version>/`.

Native gdb+CA build requires dev packages (Debian/Ubuntu):

```bash
sudo apt install libgmp-dev libmpfr-dev libmpc-dev
```

Build-time tools (when bundling): `curl` or `wget`, `zstd`, `objcopy`, `sha256sum`; for clangd also `unzip`, `strip`, `ldd`.

### Portable build (legacy glibc)

Building on a modern host (e.g. Ubuntu 24.04) pins `tgdb` to a recent glibc (e.g. `GLIBC_2.38`). To ship a binary that runs on older distros, use the Docker-based portable build:

```bash
./tools/build-portable.sh                      # Ubuntu 20.04, glibc ~2.31 baseline
./tools/build-portable.sh --static-libstdc++   # also link libstdc++/libgcc statically
./tools/build-portable.sh --bionic             # Ubuntu 18.04, glibc ~2.27 (optional)
./tools/verify-glibc.sh build/tgdb             # inspect symbols/deps only
```

Output lands in `dist/tgdb-x86_64-glibc<version>[-static-libstdc++]` plus `dist/portable-report.txt`.

Runtime requirements for the **full pack** (embedded clangd + gdb):

| Component | Dynamic deps at runtime | Typical glibc floor |
|-----------|------------------------|---------------------|
| `tgdb` | `libc`, `libm`, optional `libstdc++`/`libgcc_s` | Set by Docker image (~2.31 focal, ~2.27 bionic) |
| Embedded clangd | `libc`, `libm`, `libpthread`, `librt`, `libdl` | **≥ 2.18** (official LLVM release) |
| Embedded gdb-static | none (musl static) | none |
| Embedded gdb + Core Analyzer | depends on build | varies |

`--static-libstdc++` (also available in `compile.sh`) removes the `libstdc++.so.6` runtime dependency from `tgdb`; glibc is still required from the host.

Requires Docker. The script reuses `tools/compile.sh` inside the container with all bundles enabled.

Environment variables:

| Variable | Effect |
|----------|--------|
| `JOBS` | Parallel build jobs for `compile.sh` (default: `nproc`) |
| `CLANGD_PATH` | Override path to clangd binary (highest priority) |
| `GDB_PATH` | Override path to gdb binary (highest priority) |
| `TGDB_FORCE_BUNDLED_CLANGD` | `1` = use only embedded clangd; `0` = allow `PATH` fallback |
| `TGDB_FORCE_BUNDLED_GDB` | `1` = use only embedded gdb; `0` = allow `PATH` fallback |
| `TGDB_UI_SMOKE` | Headless UI smoke test (exits quickly, no fullscreen) |

### Running tests

```bash
cmake --build build --target text_ops_test
./build/text_ops_test
```

`text_ops_test` covers editor text operations (insert, delete, search, undo).

## Project layout

```
tgdb/
├── CMakeLists.txt          # Main build definition
├── cmake/
│   └── Dependencies.cmake  # FetchContent: FTXUI, cppdap, json
├── docs/                   # Documentation (this folder)
├── examples/
│   └── hello.cpp           # Sample debug target
├── src/
│   ├── main.cpp            # Entry point, CLI parsing
│   ├── app/                # Application orchestration
│   ├── backend/            # Debug backend (DAP)
│   ├── dap/                # GDB launcher
│   ├── editor/             # Text editor core
│   ├── indexer/            # Workspace indexing
│   ├── lsp/                # LSP client
│   ├── search/             # Project-wide search
│   ├── symbols/            # Symbol providers
│   ├── terminal/           # Shell PTY
│   ├── ui/                 # FTXUI panels and modals
│   └── util/               # Shared utilities
├── tests/
│   └── text_ops_test.cpp
└── tools/
    ├── compile.sh
    ├── build-portable.sh
    ├── verify-glibc.sh
    └── launch.sh
```

## Adding a UI panel

1. Create `src/ui/my_panel.hpp` and `.cpp`
2. Implement `ftxui::Component MakeMyPanel(...)` following existing panels
3. Wire into `MakeMainLayout` in `src/ui/main_layout.cpp`
4. Add focus region in `FocusManagerState` if the panel needs keyboard focus
5. Register the `.cpp` file in `CMakeLists.txt`

Use `MakePanel()` from `ui/panel.hpp` for consistent title/body chrome. Colours come from `ui/theme.hpp`.

## Adding a debug command

1. Add a value to `UiCommandKind` in `src/backend/idebug_backend.hpp`
2. Handle it in `DapBackend::handle_command()`
3. Emit `DebugEvent` responses as needed
4. Call from UI via `CommandCallback` / `submit_command()`

Keep all `DebugModel` mutations on the UI thread.

## Key conventions

- **C++17**, no extensions beyond the standard
- **Namespace:** `tgdb`
- **UI strings:** mixed English/Spanish in the app; documentation is English
- **Thread safety:** background threads push to `ThreadSafeQueue`; UI drains on `Event::Custom`
- **Paths:** normalize with `util/path_normalize.hpp`; store absolute paths in models
- **Modals:** implement as overlays (`MakeXOverlay`) with a `*State` struct and `open` flag; register in `Application::any_modal_open()`

## Debugging tgdb itself

Run under GDB:

```bash
gdb --args ./build/tgdb --cwd . ./build/hello
```

Crash backtraces are printed via `util/crash_handler.cpp` (`-rdynamic` enabled on Unix).

For UI issues, the status bar shows the active focus region (`[Editor]`, `[Terminal]`, …).

## Dependency versions

Pinned in `cmake/Dependencies.cmake`:

- FTXUI: `v6.1.9`
- cppdap: `main` (shallow clone)
- nlohmann/json: `v3.11.3`

## See also

- [Architecture](architecture.md)
- [Keyboard shortcuts](keyboard-shortcuts.md)
