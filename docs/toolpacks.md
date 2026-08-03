# Toolpacks

Toolpacks are versioned LSP/DAP tool payloads installed in user space and resolved
at runtime. The IDE **core** (including Tree-sitter and **rg**) ships without them.
Pilot toolpacks: **clangd** + shared **gdb** (Linux **x86_64**).

## Resolution order (clangd / gdb)

1. Env override (`CLANGD_PATH` / `GDB_PATH`)
2. Active **toolpack** (`TUIDE_TOOLPACKS_ROOT` or XDG data)
3. Compile-time **bundled** blob (temporary)
4. `PATH`
5. Missing

## Layout

```text
$XDG_DATA_HOME/tuide/toolpacks/     # default ~/.local/share/tuide/toolpacks
  manifest.json
  clangd/<version>/…
  gdb/<version>/…

$XDG_CACHE_HOME/tuide/
  downloads/
  export-work/

Override root: TUIDE_TOOLPACKS_ROOT
```

## Catalog (GitHub Releases)

- Tags: `catalog-YYYY.MM.DD` + movable `catalog-latest`
- Assets: `catalog.json`, `clangd-…tar.zst`, `gdb-…tar.zst`, `SHA256SUMS`
- Default: `https://github.com/LorenzoAdr/TIDE/releases/download/catalog-latest/catalog.json`
- Override: `TUIDE_TOOLPACKS_CATALOG_URL`

## Language packs (UI)

F10 → **Toolpacks**. Pilot: **C / C++** = `clangd` + shared `gdb`.

```bash
tuide toolpacks install cpp
./tools/publish_toolpack_catalog.sh
```

## Export (AppImage)

Portable export builds an **AppDir** (and optionally a Type‑2 **AppImage**), not an ELF blob.

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

- `TUIDE_BUNDLE_CLANGD` remains during testing; deprecate when toolpacks are default.
- ELF embed trailer export is **removed**; old trailers are only detected to block re-export.
