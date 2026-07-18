#!/usr/bin/env bash
set -euo pipefail

die() {
  printf 'prepare_gdb_static_bundle: error: %s\n' "$*" >&2
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

require_var TUIDE_GDB_VERSION
require_var TUIDE_GDB_TAR_GZ_PATH
require_var TUIDE_GDB_TAR_GZ_URL
require_var TUIDE_GDB_STAGING_DIR
require_var TUIDE_GDB_PAYLOAD_DIR
require_var TUIDE_GDB_TAR_PATH
require_var TUIDE_GDB_ZST_PATH
require_var TUIDE_GDB_MANIFEST_PATH
require_var TUIDE_GDB_MANIFEST_HPP
require_var TUIDE_GDB_BLOB_OBJ

TUIDE_GDB_VERSION="$(strip_cmake_quotes "${TUIDE_GDB_VERSION}")"
TUIDE_GDB_TAR_GZ_PATH="$(strip_cmake_quotes "${TUIDE_GDB_TAR_GZ_PATH}")"
TUIDE_GDB_TAR_GZ_URL="$(strip_cmake_quotes "${TUIDE_GDB_TAR_GZ_URL}")"
TUIDE_GDB_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_GDB_STAGING_DIR}")"
TUIDE_GDB_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_GDB_PAYLOAD_DIR}")"
TUIDE_GDB_TAR_PATH="$(strip_cmake_quotes "${TUIDE_GDB_TAR_PATH}")"
TUIDE_GDB_ZST_PATH="$(strip_cmake_quotes "${TUIDE_GDB_ZST_PATH}")"
TUIDE_GDB_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_GDB_MANIFEST_PATH}")"
TUIDE_GDB_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_GDB_MANIFEST_HPP}")"
TUIDE_GDB_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_GDB_BLOB_OBJ}")"

for tool in ldd zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

if [[ ! -f "${TUIDE_GDB_TAR_GZ_PATH}" ]]; then
  printf 'descargando %s...\n' "${TUIDE_GDB_TAR_GZ_URL}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TUIDE_GDB_TAR_GZ_PATH}" "${TUIDE_GDB_TAR_GZ_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TUIDE_GDB_TAR_GZ_PATH}" "${TUIDE_GDB_TAR_GZ_URL}"
  else
    die "curl o wget requerido para descargar gdb-static"
  fi
fi

rm -rf "${TUIDE_GDB_STAGING_DIR}"
mkdir -p "${TUIDE_GDB_STAGING_DIR}"
tar -xzf "${TUIDE_GDB_TAR_GZ_PATH}" -C "${TUIDE_GDB_STAGING_DIR}"

gdb_bin="$(find "${TUIDE_GDB_STAGING_DIR}" -type f -name gdb | head -n1)"
[[ -n "${gdb_bin}" && -f "${gdb_bin}" ]] || die "no se encontró gdb en el release"
chmod +x "${gdb_bin}"

ldd_out="$(ldd "${gdb_bin}" 2>&1 || true)"
if [[ "${ldd_out}" != *"statically linked"* ]]; then
  die "gdb-static debe estar enlazado estáticamente; ldd: ${ldd_out}"
fi

if ! "${gdb_bin}" --quiet -i=dap -ex quit >/dev/null 2>&1; then
  die "gdb empaquetado no soporta DAP (-i=dap)"
fi

rm -rf "${TUIDE_GDB_PAYLOAD_DIR}"
mkdir -p "${TUIDE_GDB_PAYLOAD_DIR}/bin"
cp "${gdb_bin}" "${TUIDE_GDB_PAYLOAD_DIR}/bin/gdb"

rm -f "${TUIDE_GDB_TAR_PATH}"
tar -cf "${TUIDE_GDB_TAR_PATH}" -C "${TUIDE_GDB_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_GDB_TAR_PATH}" -o "${TUIDE_GDB_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_GDB_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_GDB_PAYLOAD_DIR}/bin/gdb" | awk '{print $1}')"

cat > "${TUIDE_GDB_MANIFEST_PATH}" <<EOF
{
  "version": "${TUIDE_GDB_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "core_analyzer": false,
  "bundle_kind": "static"
}
EOF

cat > "${TUIDE_GDB_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_GDB_VERSION "${TUIDE_GDB_VERSION}"
#define TUIDE_BUNDLED_GDB_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_GDB_BINARY_SHA256 "${bin_sha}"
#define TUIDE_BUNDLED_GDB_HAS_CORE_ANALYZER 0
EOF

blob_dir="$(dirname "${TUIDE_GDB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_GDB_ZST_PATH}")" "$(basename "${TUIDE_GDB_BLOB_OBJ}")"
)

printf 'gdb-static bundle listo: %s (%s bytes)\n' "${TUIDE_GDB_ZST_PATH}" "$(wc -c < "${TUIDE_GDB_ZST_PATH}")"
