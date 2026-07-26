#!/usr/bin/env bash
# Empaqueta toolpacks de piloto (clangd 19.1.2 + gdb-static) y genera catalog.json
# listo para un release GitHub tag catalog-latest / catalog-YYYY.MM.DD.
#
# Uso:
#   ./tools/publish_toolpack_catalog.sh
#   ./tools/publish_toolpack_catalog.sh --out dist/catalog
#
# No sube el release (gh de CI es read-only). Imprime el comando gh sugerido.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT}/dist/catalog"
CLANGD_VERSION="${TUIDE_CLANGD_VERSION:-19.1.2}"
GDB_VERSION_TAG="${TUIDE_GDB_STATIC_VERSION:-v16.3-static}"
GDB_VERSION="${GDB_VERSION_TAG#v}"
CACHE_DIR="${ROOT}/third_party/bundled/cache"
WORK_DIR=""

log() { printf '[publish-catalog] %s\n' "$*"; }
die() { printf '[publish-catalog] error: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Uso: $(basename "$0") [--out DIR]

Genera en DIR:
  catalog.json
  clangd-${CLANGD_VERSION}-linux-x86_64.tar.zst
  gdb-${GDB_VERSION}-linux-x86_64.tar.zst
  SHA256SUMS

Variables:
  TUIDE_CLANGD_VERSION       default ${CLANGD_VERSION}
  TUIDE_GDB_STATIC_VERSION   default ${GDB_VERSION_TAG}
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --out) OUT_DIR="$2"; shift 2 ;;
    *) die "opcion desconocida: $1" ;;
  esac
done

need() { command -v "$1" >/dev/null 2>&1 || die "falta $1"; }
need curl; need unzip; need strip; need ldd; need tar; need zstd; need sha256sum

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tuide-catalog.XXXXXX")"
cleanup() { rm -rf "${WORK_DIR}"; }
trap cleanup EXIT

mkdir -p "${OUT_DIR}" "${CACHE_DIR}" "${WORK_DIR}"

# --- clangd (misma fuente que cmake/BundleClangd.cmake) ---
CLANGD_ZIP_NAME="clangd-linux-${CLANGD_VERSION}.zip"
CLANGD_ZIP_URL="https://github.com/clangd/clangd/releases/download/${CLANGD_VERSION}/${CLANGD_ZIP_NAME}"
CLANGD_ZIP_PATH="${CACHE_DIR}/${CLANGD_ZIP_NAME}"
if [[ ! -f "${CLANGD_ZIP_PATH}" ]]; then
  log "descargando clangd ${CLANGD_VERSION}..."
  curl -fL --retry 3 -o "${CLANGD_ZIP_PATH}.partial" "${CLANGD_ZIP_URL}"
  mv -f "${CLANGD_ZIP_PATH}.partial" "${CLANGD_ZIP_PATH}"
fi

CLANGD_STAGE="${WORK_DIR}/clangd_stage"
CLANGD_PAYLOAD="${WORK_DIR}/clangd_payload"
rm -rf "${CLANGD_STAGE}" "${CLANGD_PAYLOAD}"
mkdir -p "${CLANGD_STAGE}"
unzip -q "${CLANGD_ZIP_PATH}" -d "${CLANGD_STAGE}"
clangd_bin="$(find "${CLANGD_STAGE}" -mindepth 2 -type f -path '*/bin/clangd' | head -n1)"
[[ -n "${clangd_bin}" ]] || die "no se encontro bin/clangd"
chmod +x "${clangd_bin}"
clangd_root="$(dirname "$(dirname "${clangd_bin}")")"
resource_glob=( "${clangd_root}"/lib/clang/*/include )
[[ -d "${resource_glob[0]}" ]] || die "falta lib/clang/*/include"
resource_dir_rel="${resource_glob[0]#${clangd_root}/}"
resource_dir_rel="${resource_dir_rel%/include}"
strip -s "${clangd_bin}"

mkdir -p "${CLANGD_PAYLOAD}/bin" "${CLANGD_PAYLOAD}/${resource_dir_rel}"
cp "${clangd_bin}" "${CLANGD_PAYLOAD}/bin/clangd"
cp -a "${clangd_root}/${resource_dir_rel}/." "${CLANGD_PAYLOAD}/${resource_dir_rel}/"
cat > "${CLANGD_PAYLOAD}/toolpack.json" <<EOF
{
  "schema": 1,
  "id": "clangd",
  "version": "${CLANGD_VERSION}",
  "arch": "x86_64",
  "os": "linux",
  "license": "Apache-2.0 WITH LLVM-exception",
  "entry": { "type": "executable", "path": "bin/clangd", "args": [] },
  "resource_dir": "",
  "env": {}
}
EOF

CLANGD_OUT="${OUT_DIR}/clangd-${CLANGD_VERSION}-linux-x86_64.tar.zst"
tar -C "${CLANGD_PAYLOAD}" -cf - . | zstd -f -19 -q -o "${CLANGD_OUT}"
CLANGD_SHA="$(sha256sum "${CLANGD_OUT}" | awk '{print $1}')"
log "clangd -> ${CLANGD_OUT}"

# --- gdb-static (misma fuente que BundleGdb static) ---
GDB_TAR_NAME="gdb-static-full-x86_64.tar.gz"
GDB_TAR_URL="https://github.com/guyush1/gdb-static/releases/download/${GDB_VERSION_TAG}/${GDB_TAR_NAME}"
GDB_TAR_PATH="${CACHE_DIR}/${GDB_VERSION_TAG}-${GDB_TAR_NAME}"
if [[ ! -f "${GDB_TAR_PATH}" ]]; then
  log "descargando gdb-static ${GDB_VERSION_TAG}..."
  curl -fL --retry 3 -o "${GDB_TAR_PATH}.partial" "${GDB_TAR_URL}"
  mv -f "${GDB_TAR_PATH}.partial" "${GDB_TAR_PATH}"
fi

GDB_STAGE="${WORK_DIR}/gdb_stage"
GDB_PAYLOAD="${WORK_DIR}/gdb_payload"
rm -rf "${GDB_STAGE}" "${GDB_PAYLOAD}"
mkdir -p "${GDB_STAGE}" "${GDB_PAYLOAD}/bin"
tar -xzf "${GDB_TAR_PATH}" -C "${GDB_STAGE}"
gdb_bin="$(find "${GDB_STAGE}" -type f -name gdb | head -n1)"
[[ -n "${gdb_bin}" && -x "${gdb_bin}" ]] || die "no se encontro gdb en el tarball static"
cp "${gdb_bin}" "${GDB_PAYLOAD}/bin/gdb"
chmod +x "${GDB_PAYLOAD}/bin/gdb"
cat > "${GDB_PAYLOAD}/toolpack.json" <<EOF
{
  "schema": 1,
  "id": "gdb",
  "version": "${GDB_VERSION}",
  "arch": "x86_64",
  "os": "linux",
  "license": "GPL-3.0-or-later",
  "entry": { "type": "executable", "path": "bin/gdb", "args": [] },
  "resource_dir": "",
  "env": {}
}
EOF

GDB_OUT="${OUT_DIR}/gdb-${GDB_VERSION}-linux-x86_64.tar.zst"
tar -C "${GDB_PAYLOAD}" -cf - . | zstd -f -19 -q -o "${GDB_OUT}"
GDB_SHA="$(sha256sum "${GDB_OUT}" | awk '{print $1}')"
log "gdb -> ${GDB_OUT}"

REPO="${TUIDE_TOOLPACKS_REPO:-LorenzoAdr/TIDE}"
BASE_URL="https://github.com/${REPO}/releases/download/catalog-latest"

cat > "${OUT_DIR}/catalog.json" <<EOF
{
  "schema": 1,
  "tuide_min_version": "0.1.0",
  "toolpacks": [
    {
      "id": "clangd",
      "display_name": "clangd",
      "kind": "lsp",
      "languages": ["c", "cpp"],
      "version": "${CLANGD_VERSION}",
      "arch": ["x86_64"],
      "os": ["linux"],
      "url": "${BASE_URL}/clangd-${CLANGD_VERSION}-linux-x86_64.tar.zst",
      "sha256": "${CLANGD_SHA}",
      "size_bytes": $(wc -c < "${CLANGD_OUT}"),
      "license": "Apache-2.0 WITH LLVM-exception",
      "recommends_system": ["clangd"]
    },
    {
      "id": "gdb",
      "display_name": "GDB",
      "kind": "dap",
      "languages": ["c", "cpp", "rust", "go", "zig", "fortran"],
      "version": "${GDB_VERSION}",
      "arch": ["x86_64"],
      "os": ["linux"],
      "url": "${BASE_URL}/gdb-${GDB_VERSION}-linux-x86_64.tar.zst",
      "sha256": "${GDB_SHA}",
      "size_bytes": $(wc -c < "${GDB_OUT}"),
      "license": "GPL-3.0-or-later",
      "recommends_system": ["gdb"],
      "shared": true
    }
  ]
}
EOF

{
  printf '%s  %s\n' "${CLANGD_SHA}" "$(basename "${CLANGD_OUT}")"
  printf '%s  %s\n' "${GDB_SHA}" "$(basename "${GDB_OUT}")"
} > "${OUT_DIR}/SHA256SUMS"

log "catalog.json escrito en ${OUT_DIR}"
log "Para publicar (requiere escritura en GitHub):"
cat <<EOF

  TAG=catalog-\$(date +%Y.%m.%d)
  gh release create "\$TAG" \\
    --repo ${REPO} \\
    --title "Toolpack catalog \$TAG" \\
    --notes "clangd ${CLANGD_VERSION} + gdb-static ${GDB_VERSION} (x86_64)" \\
    "${OUT_DIR}/catalog.json" \\
    "${CLANGD_OUT}" \\
    "${GDB_OUT}" \\
    "${OUT_DIR}/SHA256SUMS"
  # Mantener tag movible catalog-latest apuntando a los mismos assets:
  gh release delete catalog-latest --repo ${REPO} --yes 2>/dev/null || true
  gh release create catalog-latest \\
    --repo ${REPO} \\
    --title "Toolpack catalog (latest)" \\
    --notes "Puntero movible al catalogo actual" \\
    "${OUT_DIR}/catalog.json" \\
    "${CLANGD_OUT}" \\
    "${GDB_OUT}" \\
    "${OUT_DIR}/SHA256SUMS"

EOF
