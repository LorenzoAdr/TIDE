#!/usr/bin/env bash
# Empaqueta toolpacks (LSP + DAP) y genera catalog.json para GitHub Releases
# (catalog-latest / catalog-YYYY.MM.DD).
#
# Reutiliza cmake/prepare_*.sh cuando existen (con stub de objcopy). clangd/gdb
# se empaquetan inline como en el piloto.
#
# Uso:
#   ./tools/publish_toolpack_catalog.sh
#   ./tools/publish_toolpack_catalog.sh --arch aarch64
#   ./tools/publish_toolpack_catalog.sh --arch all
#   ./tools/publish_toolpack_catalog.sh --out dist/catalog
#   ./tools/publish_toolpack_catalog.sh --only clangd,gdb,rust-analyzer
#
# --arch x86_64|aarch64|all  (default: arquitectura del host).
# Dos pasadas (--arch x86_64 y luego --arch aarch64) al mismo --out fusionan
# catalog.json. --arch all genera ambas en una sola corrida.
#
# No sube el release. Imprime el comando gh sugerido.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${ROOT}/dist/catalog"
CACHE_DIR="${ROOT}/third_party/bundled/cache"
GEN_DIR=""  # set under WORK_DIR (/tmp) after mktemp — avoids filling $HOME
ONLY=""
ARCH_ARG=""
WORK_DIR=""
STUB_BIN=""
# Set by set_arch_vars: canonical catalog arch + upstream filename pieces.
ARCH=""
UPSTREAM_RUST=""
UPSTREAM_ZLS=""
UPSTREAM_NEOCMAKE=""
UPSTREAM_MAKE_LS=""
UPSTREAM_LUA=""
UPSTREAM_TEXLAB=""
UPSTREAM_GDB=""
UPSTREAM_LEMMINX=""
UPSTREAM_LEMMINX_BIN=""
UPSTREAM_PYTHON=""
UPSTREAM_NODE=""
UPSTREAM_GO_TARGET=""
UPSTREAM_DEB=""
UPSTREAM_SHELLCHECK=""

# Versions aligned with cmake/BundleOptions.cmake defaults.
CLANGD_VERSION="${TUIDE_CLANGD_VERSION:-19.1.2}"
GDB_VERSION_TAG="${TUIDE_GDB_STATIC_VERSION:-v16.3-static}"
GDB_VERSION="${GDB_VERSION_TAG#v}"
RUST_ANALYZER_VERSION="${TUIDE_RUST_ANALYZER_VERSION:-2025-12-29}"
GOPLS_VERSION="${TUIDE_GOPLS_VERSION:-v0.23.0}"
GO_VERSION="${TUIDE_GO_VERSION:-1.26.5}"
ZLS_VERSION="${TUIDE_ZLS_VERSION:-0.16.0}"
LUA_LS_VERSION="${TUIDE_LUA_LS_VERSION:-3.18.2}"
FORTLS_VERSION="${TUIDE_FORTLS_VERSION:-3.2.2}"
NEOCMAKELSP_VERSION="${TUIDE_NEOCMAKELSP_VERSION:-v0.10.4}"
MAKE_LS_VERSION="${TUIDE_MAKE_LS_VERSION:-v0.1.16}"
TEXLAB_VERSION="${TUIDE_TEXLAB_VERSION:-5.26.0}"
CHKTEX_VERSION="${TUIDE_CHKTEX_VERSION:-1.7.10-1}"
PCRE2_VERSION="${TUIDE_PCRE2_VERSION:-10.47-2}"
BASH_LS_VERSION="${TUIDE_BASH_LS_VERSION:-5.6.0}"
BASH_LS_NPM_VERSION="${TUIDE_BASH_LS_NPM_VERSION:-5.6.0}"
BASH_DAP_VERSION="${TUIDE_BASH_DAP_VERSION:-0.3.9}"
NODE_VERSION="${TUIDE_NODE_VERSION:-22.16.0}"
TYPESCRIPT_LS_NPM_VERSION="${TUIDE_TYPESCRIPT_LS_NPM_VERSION:-5.3.0}"
TYPESCRIPT_VERSION="${TUIDE_TYPESCRIPT_VERSION:-7.0.2}"
TYPESCRIPT_LS_VERSION="${TUIDE_TYPESCRIPT_LS_VERSION:-${TYPESCRIPT_LS_NPM_VERSION}+ts${TYPESCRIPT_VERSION}}"
YAML_LS_NPM_VERSION="${TUIDE_YAML_LS_NPM_VERSION:-1.24.0}"
YAML_LS_VERSION="${TUIDE_YAML_LS_VERSION:-${YAML_LS_NPM_VERSION}}"
LEMMINX_VERSION="${TUIDE_LEMMINX_VERSION:-0.29.3}"
BASEDPYRIGHT_VERSION="${TUIDE_BASEDPYRIGHT_VERSION:-1.39.9}"
DEBUGPY_VERSION="${TUIDE_DEBUGPY_VERSION:-1.8.21}"
PYTHON_STANDALONE_VERSION="${TUIDE_PYTHON_STANDALONE_VERSION:-3.12.13}"
PYTHON_STANDALONE_TAG="${TUIDE_PYTHON_STANDALONE_TAG:-20260623}"
PYTHON_TOOLS_VERSION="${TUIDE_PYTHON_TOOLS_VERSION:-${PYTHON_STANDALONE_VERSION}+${BASEDPYRIGHT_VERSION}+${DEBUGPY_VERSION}-full}"

REPO="${TUIDE_TOOLPACKS_REPO:-LorenzoAdr/TIDE}"
BASE_URL="https://github.com/${REPO}/releases/download/catalog-latest"

# Accumulator for catalog.json (bash arrays of JSON object strings + asset paths).
CATALOG_ENTRIES=()
ASSET_PATHS=()
SHA_LINES=()

log() { printf '[publish-catalog] %s\n' "$*"; }
die() { printf '[publish-catalog] error: %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<EOF
Uso: $(basename "$0") [--out DIR] [--only id,id,...] [--arch x86_64|aarch64|all]

Genera en DIR: catalog.json, SHA256SUMS y un .tar.zst por toolpack y arch.
El cliente AppImage descarga el asset cuyo arch coincide con el host.

Variables de versión: mismas que cmake/BundleOptions.cmake (TUIDE_*_VERSION).
EOF
}

want_pack() {
  local id="$1"
  [[ -z "${ONLY}" ]] && return 0
  [[ ",${ONLY}," == *",${id},"* ]]
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help) usage; exit 0 ;;
    --out) OUT_DIR="$2"; shift 2 ;;
    --only) ONLY="$2"; shift 2 ;;
    --arch) ARCH_ARG="$2"; shift 2 ;;
    *) die "opcion desconocida: $1" ;;
  esac
done

canonical_arch() {
  case "$1" in
    x86_64|amd64|x64) printf 'x86_64\n' ;;
    aarch64|arm64) printf 'aarch64\n' ;;
    *) printf '%s\n' "$1" ;;
  esac
}

host_catalog_arch() {
  canonical_arch "$(uname -m)"
}

set_arch_vars() {
  ARCH="$(canonical_arch "$1")"
  case "${ARCH}" in
    x86_64)
      UPSTREAM_RUST="x86_64-unknown-linux-gnu"
      UPSTREAM_ZLS="x86_64-linux"
      UPSTREAM_NEOCMAKE="x86_64-unknown-linux-gnu"
      UPSTREAM_MAKE_LS="linux_amd64"
      UPSTREAM_LUA="linux-x64"
      UPSTREAM_TEXLAB="x86_64-linux"
      UPSTREAM_GDB="x86_64"
      UPSTREAM_LEMMINX="linux-x86_64"
      UPSTREAM_LEMMINX_BIN="lemminx-linux-x86_64"
      UPSTREAM_PYTHON="x86_64-unknown-linux-gnu"
      UPSTREAM_NODE="linux-x64"
      UPSTREAM_GO_TARGET="amd64"
      UPSTREAM_DEB="amd64"
      UPSTREAM_SHELLCHECK="linux.x86_64"
      ;;
    aarch64)
      UPSTREAM_RUST="aarch64-unknown-linux-gnu"
      UPSTREAM_ZLS="aarch64-linux"
      UPSTREAM_NEOCMAKE="aarch64-unknown-linux-gnu"
      UPSTREAM_MAKE_LS="linux_arm64"
      UPSTREAM_LUA="linux-arm64"
      UPSTREAM_TEXLAB="aarch64-linux"
      UPSTREAM_GDB="aarch64"
      UPSTREAM_LEMMINX="linux-aarch_64"
      UPSTREAM_LEMMINX_BIN="lemminx-linux-aarch_64"
      UPSTREAM_PYTHON="aarch64-unknown-linux-gnu"
      UPSTREAM_NODE="linux-arm64"
      UPSTREAM_GO_TARGET="arm64"
      UPSTREAM_DEB="arm64"
      UPSTREAM_SHELLCHECK="linux.aarch64"
      ;;
    *)
      die "arch no soportada: $1 (usa x86_64, aarch64 o all)"
      ;;
  esac
  log "arquitectura de empaquetado: ${ARCH}"
}

if [[ -z "${ARCH_ARG}" ]]; then
  ARCH_ARG="$(host_catalog_arch)"
fi

need() { command -v "$1" >/dev/null 2>&1 || die "falta $1"; }
need curl; need tar; need zstd; need sha256sum

WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/tuide-catalog.XXXXXX")"
GEN_DIR="${WORK_DIR}/gen"
STUB_BIN="${WORK_DIR}/stub-bin"
mkdir -p "${STUB_BIN}" "${GEN_DIR}"
# prepare_*.sh requieren objcopy para el blob embebido; aquí solo necesitamos el payload.
cat > "${STUB_BIN}/objcopy" <<'EOF'
#!/bin/sh
# Stub: crea el .o de salida vacío (último argumento).
out=
for arg in "$@"; do out=$arg; done
: > "${out}"
EOF
chmod +x "${STUB_BIN}/objcopy"
export PATH="${STUB_BIN}:${PATH}"

cleanup() { rm -rf "${WORK_DIR}"; }
trap cleanup EXIT

mkdir -p "${OUT_DIR}" "${CACHE_DIR}"

download() {
  local url="$1" dest="$2"
  if [[ -f "${dest}" ]]; then
    return 0
  fi
  log "descargando $(basename "${dest}")..."
  mkdir -p "$(dirname "${dest}")"
  curl -fL --retry 3 -o "${dest}.partial" "${url}"
  mv -f "${dest}.partial" "${dest}"
}

# Strip only when the binary matches the host, or a matching cross-strip exists.
maybe_strip() {
  local bin="$1"
  local host
  host="$(host_catalog_arch)"
  if [[ "${ARCH}" == "${host}" ]]; then
    strip -s "${bin}" || true
    return 0
  fi
  if [[ "${ARCH}" == "aarch64" ]] && command -v aarch64-linux-gnu-strip >/dev/null 2>&1; then
    aarch64-linux-gnu-strip -s "${bin}" || true
  fi
}

write_toolpack_json() {
  local payload="$1" id="$2" version="$3" entry_path="$4" license="$5"
  cat > "${payload}/toolpack.json" <<EOF
{
  "schema": 1,
  "id": "${id}",
  "version": "${version}",
  "arch": "${ARCH}",
  "os": "linux",
  "license": "${license}",
  "entry": { "type": "executable", "path": "${entry_path}", "args": [] },
  "resource_dir": "",
  "env": {}
}
EOF
}

pack_payload() {
  local payload="$1" id="$2" version="$3" entry_path="$4" license="$5"
  local kind="$6" display="$7" languages_json="$8"
  local shared="${9:-false}"
  local out sha size meta shared_json=""

  [[ -x "${payload}/${entry_path}" ]] || die "entry no ejecutable: ${payload}/${entry_path}"
  write_toolpack_json "${payload}" "${id}" "${version}" "${entry_path}" "${license}"
  out="${OUT_DIR}/${id}-${version}-linux-${ARCH}.tar.zst"
  # Sanitize filename: GitHub accepts +, but keep literal version in name.
  # Prefer faster compression for catalog packaging (override with ZSTD_LEVEL).
  tar -C "${payload}" -cf - . | zstd -f -"${ZSTD_LEVEL:-9}" -q -o "${out}"
  sha="$(sha256sum "${out}" | awk '{print $1}')"
  size="$(wc -c < "${out}" | tr -d ' ')"
  log "${id} ${ARCH} -> ${out}"

  ASSET_PATHS+=("${out}")
  SHA_LINES+=("${sha}  $(basename "${out}")")

  if [[ "${shared}" == "true" ]]; then
    shared_json=',
      "shared": true'
  fi
  CATALOG_ENTRIES+=("    {
      \"id\": \"${id}\",
      \"display_name\": \"${display}\",
      \"kind\": \"${kind}\",
      \"languages\": ${languages_json},
      \"version\": \"${version}\",
      \"arch\": [\"${ARCH}\"],
      \"os\": [\"linux\"],
      \"url\": \"${BASE_URL}/$(basename "${out}")\",
      \"sha256\": \"${sha}\",
      \"size_bytes\": ${size},
      \"license\": \"${license}\"${shared_json}
    }")
  meta="${out%.tar.zst}.entry.json"
  cat > "${meta}" <<EOF
{
  "id": "${id}",
  "display_name": "${display}",
  "kind": "${kind}",
  "languages": ${languages_json},
  "version": "${version}",
  "arch": ["${ARCH}"],
  "os": ["linux"],
  "license": "${license}",
  "shared": ${shared}
}
EOF
}

# --- clangd ---
# x86_64: zip oficial clangd/clangd. aarch64: recorte del tarball LLVM (no hay zip ARM).
pack_clangd() {
  want_pack clangd || return 0
  local stage="${WORK_DIR}/clangd_stage" payload="${WORK_DIR}/clangd_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}"
  local clangd_bin clangd_root resource_glob resource_dir_rel
  if [[ "${ARCH}" == "aarch64" ]]; then
    local tar_name="clang+llvm-${CLANGD_VERSION}-aarch64-linux-gnu.tar.xz"
    local tar_path="${CACHE_DIR}/${tar_name}"
    download "https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANGD_VERSION}/${tar_name}" \
      "${tar_path}"
    log "extrayendo clangd + lib/clang del tarball LLVM (aarch64)..."
    local members
    mapfile -t members < <(tar -tJf "${tar_path}" | grep -E '(/bin/clangd$|/lib/clang/)')
    [[ ${#members[@]} -gt 0 ]] || die "el tarball LLVM no contiene bin/clangd ni lib/clang"
    tar -xJf "${tar_path}" -C "${stage}" "${members[@]}"
  else
    need unzip
    local zip_name="clangd-linux-${CLANGD_VERSION}.zip"
    local zip_path="${CACHE_DIR}/${zip_name}"
    download "https://github.com/clangd/clangd/releases/download/${CLANGD_VERSION}/${zip_name}" "${zip_path}"
    unzip -q "${zip_path}" -d "${stage}"
  fi
  clangd_bin="$(find "${stage}" -mindepth 2 -type f -path '*/bin/clangd' | head -n1)"
  [[ -n "${clangd_bin}" ]] || die "no se encontro bin/clangd"
  chmod +x "${clangd_bin}"
  clangd_root="$(dirname "$(dirname "${clangd_bin}")")"
  resource_glob=( "${clangd_root}"/lib/clang/*/include )
  [[ -d "${resource_glob[0]}" ]] || die "falta lib/clang/*/include"
  resource_dir_rel="${resource_glob[0]#${clangd_root}/}"
  resource_dir_rel="${resource_dir_rel%/include}"
  maybe_strip "${clangd_bin}"
  mkdir -p "${payload}/bin" "${payload}/${resource_dir_rel}"
  cp "${clangd_bin}" "${payload}/bin/clangd"
  cp -a "${clangd_root}/${resource_dir_rel}/." "${payload}/${resource_dir_rel}/"
  pack_payload "${payload}" "clangd" "${CLANGD_VERSION}" "bin/clangd" \
    "Apache-2.0 WITH LLVM-exception" "lsp" "clangd" '["c","cpp"]'
  cleanup_pack_workdir clangd
}

# --- gdb static ---
pack_gdb() {
  want_pack gdb || return 0
  local tar_name="gdb-static-full-${UPSTREAM_GDB}.tar.gz"
  local tar_path="${CACHE_DIR}/${GDB_VERSION_TAG}-${tar_name}"
  download "https://github.com/guyush1/gdb-static/releases/download/${GDB_VERSION_TAG}/${tar_name}" "${tar_path}"
  local stage="${WORK_DIR}/gdb_stage" payload="${WORK_DIR}/gdb_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}" "${payload}/bin"
  tar -xzf "${tar_path}" -C "${stage}"
  local gdb_bin
  gdb_bin="$(find "${stage}" -type f -name gdb | head -n1)"
  [[ -n "${gdb_bin}" && -x "${gdb_bin}" ]] || die "no se encontro gdb"
  cp "${gdb_bin}" "${payload}/bin/gdb"
  chmod +x "${payload}/bin/gdb"
  pack_payload "${payload}" "gdb" "${GDB_VERSION}" "bin/gdb" \
    "GPL-3.0-or-later" "dap" "GDB" '["c","cpp","rust","go","zig","fortran"]' "true"
  cleanup_pack_workdir gdb
}

# Drop staging/payload/blob intermediates after packing so disk stays free.
cleanup_pack_workdir() {
  local prefix="$1"
  [[ -n "${prefix}" ]] || return 0
  # Go module caches are installed read-only; make writable before rm.
  chmod -R u+w \
    "${GEN_DIR}/${prefix}_staging" \
    "${GEN_DIR}/${prefix}_payload" \
    "${WORK_DIR}/${prefix}_stage" \
    "${WORK_DIR}/${prefix}_payload" \
    "${WORK_DIR}/${prefix}_staging" 2>/dev/null || true
  rm -rf \
    "${GEN_DIR}/${prefix}_staging" \
    "${GEN_DIR}/${prefix}_payload" \
    "${GEN_DIR}/${prefix}_blob.tar" \
    "${GEN_DIR}/${prefix}_blob.zst" \
    "${GEN_DIR}/${prefix}_blob.o" \
    "${GEN_DIR}/${prefix}_manifest.json" \
    "${GEN_DIR}/bundled_${prefix}_manifest.hpp" \
    "${WORK_DIR}/${prefix}_stage" \
    "${WORK_DIR}/${prefix}_payload" \
    "${WORK_DIR}/${prefix}_staging" 2>/dev/null || true
}

# Run a prepare_*.sh that fills PAYLOAD_DIR, then pack it.
run_prepare_and_pack() {
  local script="$1" id="$2" version="$3" entry="$4" license="$5"
  local kind="$6" display="$7" languages_json="$8"
  local payload_var="$9"
  local cleanup_prefix="${10:-}"
  want_pack "${id}" || return 0
  log "preparando ${id} via $(basename "${script}")..."
  # shellcheck disable=SC1090
  bash "${script}"
  local payload="${!payload_var}"
  [[ -d "${payload}" ]] || die "payload vacío para ${id}: ${payload_var}"
  pack_payload "${payload}" "${id}" "${version}" "${entry}" "${license}" \
    "${kind}" "${display}" "${languages_json}"
  if [[ -n "${cleanup_prefix}" ]]; then
    cleanup_pack_workdir "${cleanup_prefix}"
  fi
}

pack_rust_analyzer() {
  want_pack rust-analyzer || return 0
  local name="rust-analyzer-${UPSTREAM_RUST}.gz"
  local dl="${CACHE_DIR}/${RUST_ANALYZER_VERSION}-${name}"
  download "https://github.com/rust-lang/rust-analyzer/releases/download/${RUST_ANALYZER_VERSION}/${name}" "${dl}"
  local payload="${WORK_DIR}/rust_analyzer_payload"
  rm -rf "${payload}"
  mkdir -p "${payload}/bin"
  gunzip -c "${dl}" > "${payload}/bin/rust-analyzer"
  chmod +x "${payload}/bin/rust-analyzer"
  pack_payload "${payload}" "rust-analyzer" "${RUST_ANALYZER_VERSION}" "bin/rust-analyzer" \
    "Apache-2.0 WITH LLVM-exception" "lsp" "rust-analyzer" '["rust"]'
  cleanup_pack_workdir rust_analyzer
}

pack_zls() {
  want_pack zls || return 0
  local name="zls-${UPSTREAM_ZLS}.tar.xz"
  local dl="${CACHE_DIR}/${ZLS_VERSION}-${name}"
  download "https://github.com/zigtools/zls/releases/download/${ZLS_VERSION}/${name}" "${dl}"
  local stage="${WORK_DIR}/zls_stage" payload="${WORK_DIR}/zls_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}" "${payload}/bin"
  tar -xJf "${dl}" -C "${stage}"
  local bin
  bin="$(find "${stage}" -type f -name zls | head -n1)"
  [[ -n "${bin}" ]] || die "no se encontro zls"
  cp "${bin}" "${payload}/bin/zls"
  chmod +x "${payload}/bin/zls"
  pack_payload "${payload}" "zls" "${ZLS_VERSION}" "bin/zls" \
    "MIT" "lsp" "zls" '["zig"]'
  cleanup_pack_workdir zls
}

pack_neocmakelsp() {
  want_pack neocmakelsp || return 0
  local name="neocmakelsp-${UPSTREAM_NEOCMAKE}.tar.gz"
  local dl="${CACHE_DIR}/${NEOCMAKELSP_VERSION}-${name}"
  download "https://github.com/neocmakelsp/neocmakelsp/releases/download/${NEOCMAKELSP_VERSION}/${name}" "${dl}"
  local stage="${WORK_DIR}/neocmakelsp_stage" payload="${WORK_DIR}/neocmakelsp_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}" "${payload}/bin"
  tar -xzf "${dl}" -C "${stage}"
  local bin
  bin="$(find "${stage}" -type f -name neocmakelsp | head -n1)"
  [[ -n "${bin}" ]] || die "no se encontro neocmakelsp"
  cp "${bin}" "${payload}/bin/neocmakelsp"
  chmod +x "${payload}/bin/neocmakelsp"
  pack_payload "${payload}" "neocmakelsp" "${NEOCMAKELSP_VERSION}" "bin/neocmakelsp" \
    "MIT" "lsp" "neocmakelsp" '["cmake"]'
  cleanup_pack_workdir neocmakelsp
}

pack_make_ls() {
  want_pack make-ls || return 0
  local name="make-ls_${UPSTREAM_MAKE_LS}.tar.gz"
  local dl="${CACHE_DIR}/${MAKE_LS_VERSION}-${name}"
  download "https://github.com/owenrumney/make-ls/releases/download/${MAKE_LS_VERSION}/${name}" "${dl}"
  local stage="${WORK_DIR}/make_ls_stage" payload="${WORK_DIR}/make_ls_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}" "${payload}/bin"
  tar -xzf "${dl}" -C "${stage}"
  local bin
  bin="$(find "${stage}" -type f -name make-ls | head -n1)"
  [[ -n "${bin}" ]] || die "no se encontro make-ls"
  cp "${bin}" "${payload}/bin/make-ls"
  chmod +x "${payload}/bin/make-ls"
  pack_payload "${payload}" "make-ls" "${MAKE_LS_VERSION}" "bin/make-ls" \
    "MIT" "lsp" "make-ls" '["make"]'
  cleanup_pack_workdir make_ls
}

pack_lua_ls() {
  want_pack lua-ls || return 0
  local name="lua-language-server-${LUA_LS_VERSION}-${UPSTREAM_LUA}.tar.gz"
  local dl="${CACHE_DIR}/${name}"
  download "https://github.com/LuaLS/lua-language-server/releases/download/${LUA_LS_VERSION}/${name}" "${dl}"
  local payload="${WORK_DIR}/lua_ls_payload"
  rm -rf "${payload}"
  mkdir -p "${payload}"
  tar -xzf "${dl}" -C "${payload}"
  [[ -x "${payload}/bin/lua-language-server" ]] || die "lua-language-server incompleto"
  pack_payload "${payload}" "lua-ls" "${LUA_LS_VERSION}" "bin/lua-language-server" \
    "MIT" "lsp" "lua-language-server" '["lua"]'
  cleanup_pack_workdir lua_ls
}

pack_texlab() {
  want_pack texlab || return 0
  export TUIDE_TEXLAB_VERSION="${TEXLAB_VERSION}"
  export TUIDE_CHKTEX_VERSION="${CHKTEX_VERSION}"
  export TUIDE_PCRE2_VERSION="${PCRE2_VERSION}"
  export TUIDE_TEXLAB_URL="https://github.com/latex-lsp/texlab/releases/download/v${TEXLAB_VERSION}/texlab-${UPSTREAM_TEXLAB}.tar.gz"
  export TUIDE_TEXLAB_TAR_PATH="${CACHE_DIR}/texlab-${TEXLAB_VERSION}-${UPSTREAM_TEXLAB}.tar.gz"
  export TUIDE_CHKTEX_DEB_URL="http://deb.debian.org/debian/pool/main/c/chktex/chktex_${CHKTEX_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_CHKTEX_DEB_PATH="${CACHE_DIR}/chktex_${CHKTEX_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_PCRE2_POSIX_DEB_URL="http://deb.debian.org/debian/pool/main/p/pcre2/libpcre2-posix3_${PCRE2_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_PCRE2_POSIX_DEB_PATH="${CACHE_DIR}/libpcre2-posix3_${PCRE2_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_PCRE2_8_DEB_URL="http://deb.debian.org/debian/pool/main/p/pcre2/libpcre2-8-0_${PCRE2_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_PCRE2_8_DEB_PATH="${CACHE_DIR}/libpcre2-8-0_${PCRE2_VERSION}_${UPSTREAM_DEB}.deb"
  export TUIDE_TEXLAB_STAGING_DIR="${GEN_DIR}/texlab_staging"
  export TUIDE_TEXLAB_PAYLOAD_DIR="${GEN_DIR}/texlab_payload"
  export TUIDE_TEXLAB_TAR_PATH_OUT="${GEN_DIR}/texlab_blob.tar"
  export TUIDE_TEXLAB_ZST_PATH="${GEN_DIR}/texlab_blob.zst"
  export TUIDE_TEXLAB_MANIFEST_PATH="${GEN_DIR}/texlab_manifest.json"
  export TUIDE_TEXLAB_MANIFEST_HPP="${GEN_DIR}/bundled_texlab_manifest.hpp"
  export TUIDE_TEXLAB_BLOB_OBJ="${GEN_DIR}/texlab_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_texlab_bundle.sh" \
    "texlab" "${TEXLAB_VERSION}" "bin/texlab" "GPL-3.0-or-later" \
    "lsp" "TexLab" '["latex"]' TUIDE_TEXLAB_PAYLOAD_DIR texlab
}

pack_gopls() {
  want_pack gopls || return 0
  local host_go
  case "$(host_catalog_arch)" in
    x86_64) host_go="amd64" ;;
    aarch64) host_go="arm64" ;;
    *) die "no hay toolchain Go oficial para el host $(uname -m)" ;;
  esac
  export TUIDE_GOPLS_VERSION="${GOPLS_VERSION}"
  export TUIDE_GO_VERSION="${GO_VERSION}"
  export TUIDE_GO_URL="https://dl.google.com/go/go${GO_VERSION}.linux-${host_go}.tar.gz"
  export TUIDE_GO_TAR_PATH="${CACHE_DIR}/go${GO_VERSION}.linux-${host_go}.tar.gz"
  export GOOS=linux
  export GOARCH="${UPSTREAM_GO_TARGET}"
  export CGO_ENABLED=0
  export TUIDE_BUNDLED_CACHE_DIR="${CACHE_DIR}"
  export TUIDE_GOPLS_STAGING_DIR="${GEN_DIR}/gopls_staging"
  export TUIDE_GOPLS_PAYLOAD_DIR="${GEN_DIR}/gopls_payload"
  export TUIDE_GOPLS_TAR_PATH="${GEN_DIR}/gopls_blob.tar"
  export TUIDE_GOPLS_ZST_PATH="${GEN_DIR}/gopls_blob.zst"
  export TUIDE_GOPLS_MANIFEST_HPP="${GEN_DIR}/bundled_gopls_manifest.hpp"
  export TUIDE_GOPLS_BLOB_OBJ="${GEN_DIR}/gopls_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_gopls_bundle.sh" \
    "gopls" "${GOPLS_VERSION}" "bin/gopls" "BSD-3-Clause" \
    "lsp" "gopls" '["go"]' TUIDE_GOPLS_PAYLOAD_DIR gopls
}

pack_fortls() {
  want_pack fortls || return 0
  need python3
  export TUIDE_FORTLS_VERSION="${FORTLS_VERSION}"
  export TUIDE_FORTLS_STAGING_DIR="${GEN_DIR}/fortls_staging"
  export TUIDE_FORTLS_PAYLOAD_DIR="${GEN_DIR}/fortls_payload"
  export TUIDE_FORTLS_TAR_PATH="${GEN_DIR}/fortls_blob.tar"
  export TUIDE_FORTLS_ZST_PATH="${GEN_DIR}/fortls_blob.zst"
  export TUIDE_FORTLS_MANIFEST_HPP="${GEN_DIR}/bundled_fortls_manifest.hpp"
  export TUIDE_FORTLS_BLOB_OBJ="${GEN_DIR}/fortls_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_fortls_bundle.sh" \
    "fortls" "${FORTLS_VERSION}" "bin/fortls" "MIT" \
    "lsp" "fortls" '["fortran"]' TUIDE_FORTLS_PAYLOAD_DIR fortls
}

pack_bash_ls() {
  want_pack bash-ls || return 0
  need npm
  export TUIDE_BASH_LS_VERSION="${BASH_LS_VERSION}"
  export TUIDE_BASH_LS_NPM_VERSION="${BASH_LS_NPM_VERSION}"
  export TUIDE_NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_NODE_TAR_PATH="${CACHE_DIR}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_SHELLCHECK_TAR_NAME="shellcheck-v${TUIDE_SHELLCHECK_VERSION:-0.10.0}.${UPSTREAM_SHELLCHECK}.tar.xz"
  export TUIDE_BASH_LS_STAGING_DIR="${GEN_DIR}/bash_ls_staging"
  export TUIDE_BASH_LS_PAYLOAD_DIR="${GEN_DIR}/bash_ls_payload"
  export TUIDE_BASH_LS_TAR_PATH="${GEN_DIR}/bash_ls_blob.tar"
  export TUIDE_BASH_LS_ZST_PATH="${GEN_DIR}/bash_ls_blob.zst"
  export TUIDE_BASH_LS_MANIFEST_HPP="${GEN_DIR}/bundled_bash_ls_manifest.hpp"
  export TUIDE_BASH_LS_BLOB_OBJ="${GEN_DIR}/bash_ls_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_bash_ls_bundle.sh" \
    "bash-ls" "${BASH_LS_VERSION}" "bin/bash-language-server" "MIT" \
    "lsp" "bash-language-server" '["bash","shell"]' TUIDE_BASH_LS_PAYLOAD_DIR bash_ls
}

pack_bash_dap() {
  want_pack bash-dap || return 0
  local src="${ROOT}/third_party/bash-debug"
  [[ -f "${src}/out/bashDebug.js" ]] || {
    log "compilando vscode-bash-debug..."
    (cd "${src}" && npm install && npm run compile)
  }
  [[ -d "${src}/node_modules" ]] || (cd "${src}" && npm install)
  export TUIDE_BASH_DAP_VERSION="${BASH_DAP_VERSION}"
  export TUIDE_BASH_DEBUG_SRC="${src}"
  export TUIDE_NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_NODE_TAR_PATH="${CACHE_DIR}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_BASH_DAP_INCLUDE_NODE=1
  export TUIDE_BASH_DAP_STAGING_DIR="${GEN_DIR}/bash_dap_staging"
  export TUIDE_BASH_DAP_PAYLOAD_DIR="${GEN_DIR}/bash_dap_payload"
  export TUIDE_BASH_DAP_TAR_PATH="${GEN_DIR}/bash_dap_blob.tar"
  export TUIDE_BASH_DAP_ZST_PATH="${GEN_DIR}/bash_dap_blob.zst"
  export TUIDE_BASH_DAP_MANIFEST_PATH="${GEN_DIR}/bash_dap_manifest.json"
  export TUIDE_BASH_DAP_MANIFEST_HPP="${GEN_DIR}/bundled_bash_dap_manifest.hpp"
  export TUIDE_BASH_DAP_BLOB_OBJ="${GEN_DIR}/bash_dap_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_bash_dap_bundle.sh" \
    "bash-dap" "${BASH_DAP_VERSION}" "bashdb/bashdb" "MIT" \
    "dap" "bash-debug" '["bash","shell"]' TUIDE_BASH_DAP_PAYLOAD_DIR bash_dap
}

pack_typescript_ls() {
  want_pack typescript-ls || return 0
  need npm
  export TUIDE_TYPESCRIPT_LS_VERSION="${TYPESCRIPT_LS_VERSION}"
  export TUIDE_TYPESCRIPT_LS_NPM_VERSION="${TYPESCRIPT_LS_NPM_VERSION}"
  export TUIDE_TYPESCRIPT_VERSION="${TYPESCRIPT_VERSION}"
  export TUIDE_NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_NODE_TAR_PATH="${CACHE_DIR}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_TYPESCRIPT_LS_STAGING_DIR="${GEN_DIR}/typescript_ls_staging"
  export TUIDE_TYPESCRIPT_LS_PAYLOAD_DIR="${GEN_DIR}/typescript_ls_payload"
  export TUIDE_TYPESCRIPT_LS_TAR_PATH="${GEN_DIR}/typescript_ls_blob.tar"
  export TUIDE_TYPESCRIPT_LS_ZST_PATH="${GEN_DIR}/typescript_ls_blob.zst"
  export TUIDE_TYPESCRIPT_LS_MANIFEST_HPP="${GEN_DIR}/bundled_typescript_ls_manifest.hpp"
  export TUIDE_TYPESCRIPT_LS_BLOB_OBJ="${GEN_DIR}/typescript_ls_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_typescript_ls_bundle.sh" \
    "typescript-ls" "${TYPESCRIPT_LS_VERSION}" "bin/typescript-language-server" "MIT" \
    "lsp" "typescript-language-server" '["javascript","typescript"]' TUIDE_TYPESCRIPT_LS_PAYLOAD_DIR typescript_ls
}

pack_yaml_ls() {
  want_pack yaml-ls || return 0
  need npm
  export TUIDE_YAML_LS_VERSION="${YAML_LS_VERSION}"
  export TUIDE_YAML_LS_NPM_VERSION="${YAML_LS_NPM_VERSION}"
  export TUIDE_NODE_URL="https://nodejs.org/dist/v${NODE_VERSION}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_NODE_TAR_PATH="${CACHE_DIR}/node-v${NODE_VERSION}-${UPSTREAM_NODE}.tar.xz"
  export TUIDE_YAML_LS_STAGING_DIR="${GEN_DIR}/yaml_ls_staging"
  export TUIDE_YAML_LS_PAYLOAD_DIR="${GEN_DIR}/yaml_ls_payload"
  export TUIDE_YAML_LS_TAR_PATH="${GEN_DIR}/yaml_ls_blob.tar"
  export TUIDE_YAML_LS_ZST_PATH="${GEN_DIR}/yaml_ls_blob.zst"
  export TUIDE_YAML_LS_MANIFEST_HPP="${GEN_DIR}/bundled_yaml_ls_manifest.hpp"
  export TUIDE_YAML_LS_BLOB_OBJ="${GEN_DIR}/yaml_ls_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_yaml_ls_bundle.sh" \
    "yaml-ls" "${YAML_LS_VERSION}" "bin/yaml-language-server" "MIT" \
    "lsp" "yaml-language-server" '["yaml"]' TUIDE_YAML_LS_PAYLOAD_DIR yaml_ls
}

pack_lemminx() {
  want_pack lemminx || return 0
  need unzip
  local name="lemminx-${UPSTREAM_LEMMINX}.zip"
  local dl="${CACHE_DIR}/${LEMMINX_VERSION}-${name}"
  download "https://github.com/redhat-developer/vscode-xml/releases/download/${LEMMINX_VERSION}/${name}" "${dl}"
  local stage="${WORK_DIR}/lemminx_stage" payload="${WORK_DIR}/lemminx_payload"
  rm -rf "${stage}" "${payload}"
  mkdir -p "${stage}" "${payload}/bin"
  unzip -q "${dl}" -d "${stage}"
  local bin
  bin="$(find "${stage}" -type f -name "${UPSTREAM_LEMMINX_BIN}" | head -n1)"
  if [[ -z "${bin}" ]]; then
    bin="$(find "${stage}" -type f -name 'lemminx*' | head -n1)"
  fi
  [[ -n "${bin}" ]] || die "no se encontro lemminx"
  cp "${bin}" "${payload}/bin/lemminx"
  chmod +x "${payload}/bin/lemminx"
  pack_payload "${payload}" "lemminx" "${LEMMINX_VERSION}" "bin/lemminx" \
    "EPL-2.0" "lsp" "LemMinX" '["xml"]'
  cleanup_pack_workdir lemminx
}

pack_python_tools() {
  want_pack python-tools || return 0
  need python3
  export TUIDE_PYTHON_TOOLS_KIND="full"
  export TUIDE_BASEDPYRIGHT_VERSION="${BASEDPYRIGHT_VERSION}"
  export TUIDE_DEBUGPY_VERSION="${DEBUGPY_VERSION}"
  export TUIDE_PYTHON_STANDALONE_VERSION="${PYTHON_STANDALONE_VERSION}"
  export TUIDE_PYTHON_STANDALONE_TAG="${PYTHON_STANDALONE_TAG}"
  export TUIDE_PYTHON_TOOLS_VERSION="${PYTHON_TOOLS_VERSION}"
  export TUIDE_PYTHON_STANDALONE_URL="https://github.com/astral-sh/python-build-standalone/releases/download/${PYTHON_STANDALONE_TAG}/cpython-${PYTHON_STANDALONE_VERSION}+${PYTHON_STANDALONE_TAG}-${UPSTREAM_PYTHON}-install_only_stripped.tar.gz"
  export TUIDE_PYTHON_STANDALONE_TAR_PATH="${CACHE_DIR}/cpython-${PYTHON_STANDALONE_VERSION}+${PYTHON_STANDALONE_TAG}-${UPSTREAM_PYTHON}-install_only_stripped.tar.gz"
  export TUIDE_PYTHON_TOOLS_STAGING_DIR="${GEN_DIR}/python_tools_staging"
  export TUIDE_PYTHON_TOOLS_PAYLOAD_DIR="${GEN_DIR}/python_tools_payload"
  export TUIDE_PYTHON_TOOLS_TAR_PATH="${GEN_DIR}/python_tools_blob.tar"
  export TUIDE_PYTHON_TOOLS_ZST_PATH="${GEN_DIR}/python_tools_blob.zst"
  export TUIDE_PYTHON_TOOLS_MANIFEST_PATH="${GEN_DIR}/python_tools_manifest.json"
  export TUIDE_PYTHON_TOOLS_MANIFEST_HPP="${GEN_DIR}/bundled_python_tools_manifest.hpp"
  export TUIDE_PYTHON_TOOLS_BLOB_OBJ="${GEN_DIR}/python_tools_blob.o"
  run_prepare_and_pack "${ROOT}/cmake/prepare_python_tools_bundle.sh" \
    "python-tools" "${PYTHON_TOOLS_VERSION}" "bin/basedpyright-langserver" "MIT" \
    "lsp" "python-tools" '["python"]' TUIDE_PYTHON_TOOLS_PAYLOAD_DIR python_tools
}

pack_all_for_arch() {
  set_arch_vars "$1"
  pack_clangd
  pack_gdb
  pack_rust_analyzer
  pack_gopls
  pack_zls
  pack_lua_ls
  pack_fortls
  pack_neocmakelsp
  pack_make_ls
  pack_texlab
  pack_bash_ls
  pack_bash_dap
  pack_typescript_ls
  pack_yaml_ls
  pack_lemminx
  pack_python_tools
}

# --- build ---
if [[ "${ARCH_ARG}" == "all" ]]; then
  pack_all_for_arch x86_64
  pack_all_for_arch aarch64
else
  pack_all_for_arch "${ARCH_ARG}"
fi

# Fold in archives already in OUT_DIR (other arch, or --only resume) so
# catalog.json lists x86_64 + aarch64 after sequential runs.
fold_existing_archives() {
  declare -A seen_assets=()
  if ((${#ASSET_PATHS[@]} > 0)); then
    for a in "${ASSET_PATHS[@]}"; do
      seen_assets["$(basename "${a}")"]=1
    done
  fi
  shopt -s nullglob
  local archive base local_stem local_id local_ver local_arch sha size meta shared_json
  for archive in "${OUT_DIR}"/*-linux-x86_64.tar.zst "${OUT_DIR}"/*-linux-aarch64.tar.zst; do
    [[ -f "${archive}" ]] || continue
    base="$(basename "${archive}")"
    [[ -n "${seen_assets[$base]:-}" ]] && continue
    local_arch="x86_64"
    local_stem="${base}"
    if [[ "${base}" == *-linux-aarch64.tar.zst ]]; then
      local_arch="aarch64"
      local_stem="${base%-linux-aarch64.tar.zst}"
    else
      local_stem="${base%-linux-x86_64.tar.zst}"
    fi
    local_id=""
    local_ver=""
    for cand in python-tools typescript-ls rust-analyzer bash-dap bash-ls yaml-ls lemminx make-ls lua-ls neocmakelsp clangd gopls fortls texlab zls gdb; do
      if [[ "${local_stem}" == "${cand}-"* ]]; then
        local_id="${cand}"
        local_ver="${local_stem#${cand}-}"
        break
      fi
    done
    [[ -n "${local_id}" ]] || continue
    sha="$(sha256sum "${archive}" | awk '{print $1}')"
    size="$(wc -c < "${archive}" | tr -d ' ')"
    log "reusando asset existente: ${base}"
    ASSET_PATHS+=("${archive}")
    SHA_LINES+=("${sha}  ${base}")
    meta="${archive%.tar.zst}.entry.json"
    if [[ -f "${meta}" ]] && command -v python3 >/dev/null 2>&1; then
      CATALOG_ENTRIES+=("$(python3 - "${meta}" "${BASE_URL}" "${base}" "${sha}" "${size}" <<'PY'
import json, sys
meta_path, base_url, base, sha, size = sys.argv[1:6]
doc = json.load(open(meta_path))
doc["url"] = f"{base_url}/{base}"
doc["sha256"] = sha
doc["size_bytes"] = int(size)
if not doc.get("shared"):
    doc.pop("shared", None)
print("    " + json.dumps(doc, ensure_ascii=False))
PY
)")
    else
      CATALOG_ENTRIES+=("    {
      \"id\": \"${local_id}\",
      \"display_name\": \"${local_id}\",
      \"kind\": \"lsp\",
      \"languages\": [],
      \"version\": \"${local_ver}\",
      \"arch\": [\"${local_arch}\"],
      \"os\": [\"linux\"],
      \"url\": \"${BASE_URL}/${base}\",
      \"sha256\": \"${sha}\",
      \"size_bytes\": ${size},
      \"license\": \"UNKNOWN\"
    }")
    fi
  done
  shopt -u nullglob
}

fold_existing_archives

[[ ${#CATALOG_ENTRIES[@]} -gt 0 ]] || die "no se genero ningun toolpack (revisa --only)"

{
  printf '{\n  "schema": 1,\n  "tuide_min_version": "0.1.0",\n  "toolpacks": [\n'
  local_i=0
  for local_i in "${!CATALOG_ENTRIES[@]}"; do
    printf '%s' "${CATALOG_ENTRIES[$local_i]}"
    if [[ $local_i -lt $((${#CATALOG_ENTRIES[@]} - 1)) ]]; then
      printf ',\n'
    else
      printf '\n'
    fi
  done
  printf '  ]\n}\n'
} > "${OUT_DIR}/catalog.json"

{
  for line in "${SHA_LINES[@]}"; do
    printf '%s\n' "${line}"
  done
} > "${OUT_DIR}/SHA256SUMS"

ASSET_PATHS+=("${OUT_DIR}/catalog.json" "${OUT_DIR}/SHA256SUMS")

log "catalog.json escrito en ${OUT_DIR} (${#CATALOG_ENTRIES[@]} toolpacks)"
log "Para publicar (requiere escritura en GitHub):"
{
  printf '\n'
  printf '  TAG=catalog-$(date +%%Y.%%m.%%d)\n'
  printf '  gh release create "$TAG" \\\n'
  printf '    --repo %s \\\n' "${REPO}"
  printf '    --title "Toolpack catalog $TAG" \\\n'
  printf '    --notes "LSP/DAP toolpacks (linux x86_64 + aarch64)" \\\n'
  n=${#ASSET_PATHS[@]}
  for i in "${!ASSET_PATHS[@]}"; do
    if [[ $i -lt $((n - 1)) ]]; then
      printf '    "%s" \\\n' "${ASSET_PATHS[$i]}"
    else
      printf '    "%s"\n' "${ASSET_PATHS[$i]}"
    fi
  done
  printf '  gh release delete catalog-latest --repo %s --yes 2>/dev/null || true\n' "${REPO}"
  printf '  gh release create catalog-latest \\\n'
  printf '    --repo %s \\\n' "${REPO}"
  printf '    --title "Toolpack catalog (latest)" \\\n'
  printf '    --notes "Puntero movible al catalogo actual (x86_64 + aarch64)" \\\n'
  for i in "${!ASSET_PATHS[@]}"; do
    if [[ $i -lt $((n - 1)) ]]; then
      printf '    "%s" \\\n' "${ASSET_PATHS[$i]}"
    else
      printf '    "%s"\n' "${ASSET_PATHS[$i]}"
    fi
  done
  printf '\n'
}