#!/usr/bin/env bash
set -euo pipefail

die() {
  printf 'prepare_gdb_bundle: error: %s\n' "$*" >&2
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
require_var TGDB_GDB_TAR_GZ_URL
require_var TGDB_GDB_STAGING_DIR
require_var TGDB_GDB_PAYLOAD_DIR
require_var TGDB_GDB_TAR_PATH
require_var TGDB_GDB_ZST_PATH
require_var TGDB_GDB_MANIFEST_PATH
require_var TGDB_GDB_MANIFEST_HPP
require_var TGDB_GDB_BLOB_OBJ

TGDB_GDB_VERSION="$(strip_cmake_quotes "${TGDB_GDB_VERSION}")"
TGDB_GDB_TAR_GZ_PATH="$(strip_cmake_quotes "${TGDB_GDB_TAR_GZ_PATH}")"
TGDB_GDB_TAR_GZ_URL="$(strip_cmake_quotes "${TGDB_GDB_TAR_GZ_URL}")"
TGDB_GDB_STAGING_DIR="$(strip_cmake_quotes "${TGDB_GDB_STAGING_DIR}")"
TGDB_GDB_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_GDB_PAYLOAD_DIR}")"
TGDB_GDB_TAR_PATH="$(strip_cmake_quotes "${TGDB_GDB_TAR_PATH}")"
TGDB_GDB_ZST_PATH="$(strip_cmake_quotes "${TGDB_GDB_ZST_PATH}")"
TGDB_GDB_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_GDB_MANIFEST_PATH}")"
TGDB_GDB_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_GDB_MANIFEST_HPP}")"
TGDB_GDB_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_GDB_BLOB_OBJ}")"

for tool in ldd zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

if [[ ! -f "${TGDB_GDB_TAR_GZ_PATH}" ]]; then
  printf 'descargando %s...\n' "${TGDB_GDB_TAR_GZ_URL}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TGDB_GDB_TAR_GZ_PATH}" "${TGDB_GDB_TAR_GZ_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TGDB_GDB_TAR_GZ_PATH}" "${TGDB_GDB_TAR_GZ_URL}"
  else
    die "curl o wget requerido para descargar gdb-static"
  fi
fi

rm -rf "${TGDB_GDB_STAGING_DIR}"
mkdir -p "${TGDB_GDB_STAGING_DIR}"
tar -xzf "${TGDB_GDB_TAR_GZ_PATH}" -C "${TGDB_GDB_STAGING_DIR}"

gdb_bin="$(find "${TGDB_GDB_STAGING_DIR}" -type f -name gdb | head -n1)"
[[ -n "${gdb_bin}" && -f "${gdb_bin}" ]] || die "no se encontró gdb en el release"
chmod +x "${gdb_bin}"

ldd_out="$(ldd "${gdb_bin}" 2>&1 || true)"
if [[ "${ldd_out}" != *"statically linked"* ]]; then
  die "gdb-static debe estar enlazado estáticamente; ldd: ${ldd_out}"
fi

if ! "${gdb_bin}" --quiet -i=dap -ex quit >/dev/null 2>&1; then
  die "gdb empaquetado no soporta DAP (-i=dap)"
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
  "binary_sha256": "${bin_sha}"
}
EOF

cat > "${TGDB_GDB_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_GDB_VERSION "${TGDB_GDB_VERSION}"
#define TGDB_BUNDLED_GDB_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_GDB_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_GDB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_GDB_ZST_PATH}")" "$(basename "${TGDB_GDB_BLOB_OBJ}")"
)

printf 'gdb bundle listo: %s (%s bytes)\n' "${TGDB_GDB_ZST_PATH}" "$(wc -c < "${TGDB_GDB_ZST_PATH}")"
