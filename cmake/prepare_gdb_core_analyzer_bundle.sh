#!/usr/bin/env bash
set -euo pipefail

die() {
  printf 'prepare_gdb_core_analyzer_bundle: error: %s\n' "$*" >&2
  exit 1
}

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() {
  if [[ -z "${!1:-}" ]]; then
    die "variable de entorno requerida: $1"
  fi
}

require_var TGDB_GDB_VERSION
require_var TGDB_GDB_TAR_GZ_PATH
require_var TGDB_GDB_STAGING_DIR
require_var TGDB_GDB_PAYLOAD_DIR
require_var TGDB_GDB_TAR_PATH
require_var TGDB_GDB_ZST_PATH
require_var TGDB_GDB_MANIFEST_PATH
require_var TGDB_GDB_MANIFEST_HPP
require_var TGDB_GDB_BLOB_OBJ

TGDB_GDB_VERSION="$(strip_cmake_quotes "${TGDB_GDB_VERSION}")"
TGDB_GDB_TAR_GZ_PATH="$(strip_cmake_quotes "${TGDB_GDB_TAR_GZ_PATH}")"
TGDB_GDB_STAGING_DIR="$(strip_cmake_quotes "${TGDB_GDB_STAGING_DIR}")"
TGDB_GDB_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_GDB_PAYLOAD_DIR}")"
TGDB_GDB_TAR_PATH="$(strip_cmake_quotes "${TGDB_GDB_TAR_PATH}")"
TGDB_GDB_ZST_PATH="$(strip_cmake_quotes "${TGDB_GDB_ZST_PATH}")"
TGDB_GDB_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_GDB_MANIFEST_PATH}")"
TGDB_GDB_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_GDB_MANIFEST_HPP}")"
TGDB_GDB_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_GDB_BLOB_OBJ}")"

TGDB_BUILD_GDB_CA="$(strip_cmake_quotes "${TGDB_BUILD_GDB_CA}")"
TGDB_GDB_CA_BUILD_SCRIPT="$(strip_cmake_quotes "${TGDB_GDB_CA_BUILD_SCRIPT}")"

is_truthy() {
  case "$(printf '%s' "$1" | tr '[:upper:]' '[:lower:]')" in
    1 | on | true | yes) return 0 ;;
    *) return 1 ;;
  esac
}

for tool in ldd zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

ensure_gdb_ca_tarball() {
  if [[ -f "${TGDB_GDB_TAR_GZ_PATH}" ]]; then
    return 0
  fi

  if ! is_truthy "${TGDB_BUILD_GDB_CA}"; then
    printf 'prepare_gdb_core_analyzer_bundle: tarball ausente; compilando gdb+core analyzer...\n' >&2
  else
    printf 'compilando gdb + core analyzer (puede tardar varios minutos)...\n'
  fi

  local build_script="${TGDB_GDB_CA_BUILD_SCRIPT}"
  if [[ -z "${build_script}" ]]; then
    build_script="$(dirname "$0")/build_gdb_core_analyzer.sh"
  fi
  [[ -x "${build_script}" ]] || chmod +x "${build_script}"
  "${build_script}" "${TGDB_GDB_TAR_GZ_PATH}"
}

ensure_gdb_ca_tarball

rm -rf "${TGDB_GDB_STAGING_DIR}"
mkdir -p "${TGDB_GDB_STAGING_DIR}"
tar -xzf "${TGDB_GDB_TAR_GZ_PATH}" -C "${TGDB_GDB_STAGING_DIR}"

gdb_bin="$(find "${TGDB_GDB_STAGING_DIR}" -type f -name gdb | head -n1)"
[[ -n "${gdb_bin}" && -f "${gdb_bin}" ]] || die "no se encontró gdb en el tarball"
chmod +x "${gdb_bin}"

if ! "${gdb_bin}" --quiet -i=dap -ex quit >/dev/null 2>&1; then
  die "gdb empaquetado no soporta DAP (-i=dap)"
fi

if ! "${gdb_bin}" --quiet -ex "help obj" -ex quit 2>&1 | grep -qi obj; then
  die "gdb empaquetado no incluye comandos core analyzer"
fi

ldd_out="$(ldd "${gdb_bin}" 2>&1 || true)"
if [[ "${ldd_out}" == *"not a dynamic executable"* ]] || [[ "${ldd_out}" == *"statically linked"* ]]; then
  printf 'gdb enlazado estáticamente\n'
else
  printf 'aviso: gdb tiene dependencias dinámicas: %s\n' "${ldd_out}"
fi

rm -rf "${TGDB_GDB_PAYLOAD_DIR}"
mkdir -p "${TGDB_GDB_PAYLOAD_DIR}/bin"
cp "${gdb_bin}" "${TGDB_GDB_PAYLOAD_DIR}/bin/gdb"

rm -f "${TGDB_GDB_TAR_PATH}"
tar -cf "${TGDB_GDB_TAR_PATH}" -C "${TGDB_GDB_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_GDB_TAR_PATH}" -o "${TGDB_GDB_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_GDB_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_GDB_PAYLOAD_DIR}/bin/gdb" | awk '{print $1}')"

cat > "${TGDB_GDB_MANIFEST_PATH}" <<EOF
{
  "version": "${TGDB_GDB_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "core_analyzer": true,
  "bundle_kind": "core_analyzer"
}
EOF

cat > "${TGDB_GDB_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_GDB_VERSION "${TGDB_GDB_VERSION}"
#define TGDB_BUNDLED_GDB_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_GDB_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_GDB_HAS_CORE_ANALYZER 1
EOF

blob_dir="$(dirname "${TGDB_GDB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_GDB_ZST_PATH}")" "$(basename "${TGDB_GDB_BLOB_OBJ}")"
)

printf 'gdb+core_analyzer bundle listo: %s (%s bytes)\n' "${TGDB_GDB_ZST_PATH}" "$(wc -c < "${TGDB_GDB_ZST_PATH}")"
