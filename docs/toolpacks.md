# Toolpacks

Toolpacks are versioned LSP/DAP tool payloads installed in user space and resolved
at runtime. The IDE **core** (including Tree-sitter and **rg**) ships without them.
Language packs in F10 group the toolpacks needed per language.

## Resolution order

1. Env override (e.g. `CLANGD_PATH`, `TUIDE_RUST_ANALYZER`, …)
2. Active **toolpack** (`TUIDE_TOOLPACKS_ROOT` or XDG data)
3. Compile-time **bundled** blob (`TUIDE_BUNDLE_*`, transitional)
4. `PATH`
5. Missing

## Layout

```text
$XDG_DATA_HOME/tuide/toolpacks/     # default ~/.local/share/tuide/toolpacks
  manifest.json
  clangd/<version>/…
  rust-analyzer/<version>/…
  python-tools/<version>/…
  …

$XDG_CACHE_HOME/tuide/
  downloads/
  export-work/

Override root: TUIDE_TOOLPACKS_ROOT
```

## Catalog (GitHub Releases)

- Tags: `catalog-YYYY.MM.DD` + movable `catalog-latest`
- Assets: `catalog.json`, one `*-linux-x86_64.tar.zst` per toolpack, `SHA256SUMS`
- Default: `https://github.com/LorenzoAdr/TIDE/releases/download/catalog-latest/catalog.json`
- Override: `TUIDE_TOOLPACKS_CATALOG_URL` (HTTP, `file://`, or absolute path)

Publish locally (does not upload):

```bash
./tools/publish_toolpack_catalog.sh
./tools/publish_toolpack_catalog.sh --only clangd,gdb,rust-analyzer
```

## Language packs (UI F10 → Toolpacks)

| Pack | Components |
|------|------------|
| `cpp` | `clangd` + shared `gdb` |
| `python` | `python-tools` (basedpyright + debugpy) |
| `bash` | `bash-ls` (+ shellcheck) + `bash-dap` |
| `latex` | `texlab` (+ chktex) |
| `rust` | `rust-analyzer` + shared `gdb` |
| `go` | `gopls` + shared `gdb` |
| `zig` | `zls` + shared `gdb` |
| `fortran` | `fortls` + shared `gdb` |
| `lua` | `lua-ls` |
| `typescript` | `typescript-ls` |
| `cmake` | `neocmakelsp` |
| `make` | `make-ls` |
| `yaml` | `yaml-ls` |

Removing a language pack drops its non-shared components; **gdb** stays if other packs need it.

```bash
tuide toolpacks install cpp
tuide toolpacks install rust-analyzer   # single toolpack id
tuide toolpacks doctor
./tools/publish_toolpack_catalog.sh
```

## Export (AppImage)

Portable export builds an **AppDir** (and optionally a Type‑2 **AppImage**), not an ELF blob.
From F10 → Toolpacks, use **Exportar portable (AppImage)** (writes `~/tuide-x86_64.AppImage`,
or `~/tuide.AppDir` if `appimagetool` is missing). CLI:

```bash
tuide export-portable --all-installed -o dist/tuide-x86_64.AppImage
tuide export-portable --format=appdir -o dist/tuide.AppDir
tuide export-portable --core-only -o dist/tuide-core.AppImage
```

### Official GitHub releases (core only)

Pushing a tag `v*` runs `.github/workflows/release-appimage.yml`:

1. Compiles a **slim** core in Docker (Ubuntu 18.04 / glibc **2.27**, `--static-libstdc++`, no bundles).
2. Packages `dist/tuide-${VERSION}.AppImage` via `export-portable --core-only`.
3. Creates a GitHub Release with that AppImage + `SHA256SUMS`.

Locally:

```bash
./tools/build-release-appimage.sh --version 0.1.0
# optional: --publish  (gh release create v0.1.0 …)
```

Asset name: `tuide-0.1.0.AppImage` from tag `v0.1.0` (leading `v` stripped).
LSP/DAP come later from the toolpack catalog (`catalog-latest`), not from this AppImage.

```text
tuide-x86_64.AppImage  (or tuide.AppDir/)
└─ AppDir/
   ├─ AppRun
   ├─ tuide.desktop
   └─ usr/
      ├─ bin/tuide
      └─ share/tuide/toolpacks/   # copied from the local store
```

`AppRun` sets `TUIDE_TOOLPACKS_ROOT` to the embedded toolpacks tree and execs `usr/bin/tuide`.

Empty `--toolpacks` / UI export includes **all active** toolpacks in the local store.
Use `--core-only` for a slim AppImage with **no** toolpacks (official releases).

### Clean core only

Export is **blocked** if the source is already packaged:

- legacy ELF trailer `TUIDTPK1`, or
- `*.AppImage`, or
- an AppDir (`AppRun` + `usr/bin/tuide`)

Use a slim core (PPA / build without export packaging).

### CLI

```bash
tuide export-portable -o dist/tuide-x86_64.AppImage
tuide export-portable --format=appdir -o dist/tuide.AppDir
tuide export-portable --toolpacks clangd,gdb --binary ./build/tuide
```

`--format=appimage` (default) requires `appimagetool` on `PATH` (or `APPIMAGETOOL`),
plus host helpers `file` and `mksquashfs` (`squashfs-tools`).
Export sets `ARCH=x86_64` and, if `mksquashfs` sits next to `appimagetool`, prepends that
directory to `PATH` (typical when using the official appimagetool AppImage extract).
`--format=appdir` always writes a runnable directory (no appimagetool).

```bash
# Example: extracted appimagetool
export APPIMAGETOOL=/path/to/squashfs-root/usr/bin/appimagetool
tuide export-portable -o dist/tuide-x86_64.AppImage --binary ./build/tuide
```

## Transition from CMake bundles / ELF trailer

- `TUIDE_BUNDLE_*` remain during testing; toolpacks win over them at resolve time.
- ELF embed trailer export is **removed**; old trailers are only detected to block re-export.
- Prefer a slim core + `tuide toolpacks install <lang>` (or AppImage export with packs).
