#!/usr/bin/env bash
set -euo pipefail

die() {
  printf 'prepare_rg_bundle: error: %s\n' "$*" >&2
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

require_var TGDB_RG_VERSION
require_var TGDB_RG_TAR_GZ_PATH
require_var TGDB_RG_TAR_GZ_URL
require_var TGDB_RG_STAGING_DIR
require_var TGDB_RG_PAYLOAD_DIR
require_var TGDB_RG_TAR_PATH
require_var TGDB_RG_ZST_PATH
require_var TGDB_RG_MANIFEST_PATH
require_var TGDB_RG_MANIFEST_HPP
require_var TGDB_RG_BLOB_OBJ

TGDB_RG_VERSION="$(strip_cmake_quotes "${TGDB_RG_VERSION}")"
TGDB_RG_TAR_GZ_PATH="$(strip_cmake_quotes "${TGDB_RG_TAR_GZ_PATH}")"
TGDB_RG_TAR_GZ_URL="$(strip_cmake_quotes "${TGDB_RG_TAR_GZ_URL}")"
TGDB_RG_STAGING_DIR="$(strip_cmake_quotes "${TGDB_RG_STAGING_DIR}")"
TGDB_RG_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_RG_PAYLOAD_DIR}")"
TGDB_RG_TAR_PATH="$(strip_cmake_quotes "${TGDB_RG_TAR_PATH}")"
TGDB_RG_ZST_PATH="$(strip_cmake_quotes "${TGDB_RG_ZST_PATH}")"
TGDB_RG_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_RG_MANIFEST_PATH}")"
TGDB_RG_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_RG_MANIFEST_HPP}")"
TGDB_RG_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_RG_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

if [[ ! -f "${TGDB_RG_TAR_GZ_PATH}" ]]; then
  printf 'descargando %s...\n' "${TGDB_RG_TAR_GZ_URL}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TGDB_RG_TAR_GZ_PATH}" "${TGDB_RG_TAR_GZ_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TGDB_RG_TAR_GZ_PATH}" "${TGDB_RG_TAR_GZ_URL}"
  else
    die "curl o wget requerido para descargar ripgrep"
  fi
fi

rm -rf "${TGDB_RG_STAGING_DIR}"
mkdir -p "${TGDB_RG_STAGING_DIR}"
tar -xzf "${TGDB_RG_TAR_GZ_PATH}" -C "${TGDB_RG_STAGING_DIR}"

rg_bin="$(find "${TGDB_RG_STAGING_DIR}" -type f -name rg | head -n1)"
[[ -n "${rg_bin}" && -f "${rg_bin}" ]] || die "no se encontró rg en el release"
chmod +x "${rg_bin}"

ldd_out="$(ldd "${rg_bin}" 2>&1 || true)"
if [[ "${ldd_out}" != *"statically linked"* ]]; then
  die "rg empaquetado debe estar enlazado estáticamente; ldd: ${ldd_out}"
fi

if ! "${rg_bin}" --version >/dev/null 2>&1; then
  die "rg empaquetado no ejecuta --version"
fi

rm -rf "${TGDB_RG_PAYLOAD_DIR}"
mkdir -p "${TGDB_RG_PAYLOAD_DIR}/bin"
cp "${rg_bin}" "${TGDB_RG_PAYLOAD_DIR}/bin/rg"

rm -f "${TGDB_RG_TAR_PATH}"
tar -cf "${TGDB_RG_TAR_PATH}" -C "${TGDB_RG_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_RG_TAR_PATH}" -o "${TGDB_RG_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_RG_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_RG_PAYLOAD_DIR}/bin/rg" | awk '{print $1}')"

cat > "${TGDB_RG_MANIFEST_PATH}" <<EOF
{
  "version": "${TGDB_RG_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}"
}
EOF

cat > "${TGDB_RG_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_RG_VERSION "${TGDB_RG_VERSION}"
#define TGDB_BUNDLED_RG_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_RG_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_RG_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_RG_ZST_PATH}")" "$(basename "${TGDB_RG_BLOB_OBJ}")"
)

printf 'rg bundle listo: %s (%s bytes)\n' "${TGDB_RG_ZST_PATH}" "$(wc -c < "${TGDB_RG_ZST_PATH}")"
