# Toolpacks

Toolpacks are versioned LSP/DAP tool payloads installed in user space and resolved
at runtime. The IDE **core** (including Tree-sitter and **rg**) ships without them.
The pilot toolpack is **clangd** only (Linux **x86_64**).

## Resolution order (clangd)

1. `CLANGD_PATH` (environment)
2. Active **toolpack**
3. Compile-time **bundled** blob (temporary; deprecated when toolpacks are stable)
4. `PATH`
5. Missing

## Layout

```text
$XDG_DATA_HOME/tuide/toolpacks/     # default ~/.local/share/tuide/toolpacks
  manifest.json
  clangd/<version>/
    toolpack.json
    bin/clangd
    lib/clang/...                   # resource-dir (optional but expected for clangd)

$XDG_CACHE_HOME/tuide/
  downloads/                        # catalog / tarball cache
  export-work/                      # export scratch

Override root: TUIDE_TOOLPACKS_ROOT
```

## Catalog (GitHub Releases, separate tags)

- Tags: `catalog-vN` or `catalog-YYYY.MM.DD`; maintain a movable `catalog-latest`.
- Assets (pilot):
  - `catalog.json`
  - `clangd-<version>-linux-x86_64.tar.zst`
  - `SHA256SUMS` (or per-asset `.sha256`)
- Default fetch:
  `https://github.com/LorenzoAdr/TIDE/releases/download/catalog-latest/catalog.json`
- Override: `TUIDE_TOOLPACKS_CATALOG_URL`

Toolpacks stay on GitHub; they are **not** required on the PPA (PPA = core only).

## Schemas

See examples under [`schemas/`](schemas/):

| File | Role |
|------|------|
| `catalog.example.json` | Remote index |
| `manifest.example.json` | Local install state |
| `toolpack.clangd.example.json` | Payload metadata inside a version dir |

`schema` field is an integer; readers accept `1`.

## CLI

```text
tuide toolpacks list
tuide toolpacks doctor
tuide toolpacks install clangd[@version]
tuide toolpacks update [clangd]
tuide toolpacks remove clangd
tuide export-portable [--toolpacks clangd | --all-installed] [-o path]
```

## Export (embed)

- Takes a **clean** core binary (no embedded toolpacks) and embeds selected toolpacks.
- If the source binary already contains an embed trailer, export is **blocked**.
- Output is a new ELF under `dist/` (never overwrite the running binary).
- Runtime of an exported binary resolves embedded toolpacks like a local toolpack
  (still below `CLANGD_PATH`).

## Transition from CMake bundles

`TUIDE_BUNDLE_CLANGD` remains available during testing. When toolpack install +
resolve + export are validated, that bundle path is deprecated for clangd.
