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

require_var TUIDE_RG_VERSION
require_var TUIDE_RG_TAR_GZ_PATH
require_var TUIDE_RG_TAR_GZ_URL
require_var TUIDE_RG_STAGING_DIR
require_var TUIDE_RG_PAYLOAD_DIR
require_var TUIDE_RG_TAR_PATH
require_var TUIDE_RG_ZST_PATH
require_var TUIDE_RG_MANIFEST_PATH
require_var TUIDE_RG_MANIFEST_HPP
require_var TUIDE_RG_BLOB_OBJ

TUIDE_RG_VERSION="$(strip_cmake_quotes "${TUIDE_RG_VERSION}")"
TUIDE_RG_TAR_GZ_PATH="$(strip_cmake_quotes "${TUIDE_RG_TAR_GZ_PATH}")"
TUIDE_RG_TAR_GZ_URL="$(strip_cmake_quotes "${TUIDE_RG_TAR_GZ_URL}")"
TUIDE_RG_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_RG_STAGING_DIR}")"
TUIDE_RG_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_RG_PAYLOAD_DIR}")"
TUIDE_RG_TAR_PATH="$(strip_cmake_quotes "${TUIDE_RG_TAR_PATH}")"
TUIDE_RG_ZST_PATH="$(strip_cmake_quotes "${TUIDE_RG_ZST_PATH}")"
TUIDE_RG_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_RG_MANIFEST_PATH}")"
TUIDE_RG_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_RG_MANIFEST_HPP}")"
TUIDE_RG_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_RG_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

if [[ ! -f "${TUIDE_RG_TAR_GZ_PATH}" ]]; then
  printf 'descargando %s...\n' "${TUIDE_RG_TAR_GZ_URL}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TUIDE_RG_TAR_GZ_PATH}" "${TUIDE_RG_TAR_GZ_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TUIDE_RG_TAR_GZ_PATH}" "${TUIDE_RG_TAR_GZ_URL}"
  else
    die "curl o wget requerido para descargar ripgrep"
  fi
fi

rm -rf "${TUIDE_RG_STAGING_DIR}"
mkdir -p "${TUIDE_RG_STAGING_DIR}"
tar -xzf "${TUIDE_RG_TAR_GZ_PATH}" -C "${TUIDE_RG_STAGING_DIR}"

rg_bin="$(find "${TUIDE_RG_STAGING_DIR}" -type f -name rg | head -n1)"
[[ -n "${rg_bin}" && -f "${rg_bin}" ]] || die "no se encontró rg en el release"
chmod +x "${rg_bin}"

ldd_out="$(ldd "${rg_bin}" 2>&1 || true)"
if [[ "${ldd_out}" != *"statically linked"* ]]; then
  die "rg empaquetado debe estar enlazado estáticamente; ldd: ${ldd_out}"
fi

if ! "${rg_bin}" --version >/dev/null 2>&1; then
  die "rg empaquetado no ejecuta --version"
fi

rm -rf "${TUIDE_RG_PAYLOAD_DIR}"
mkdir -p "${TUIDE_RG_PAYLOAD_DIR}/bin"
cp "${rg_bin}" "${TUIDE_RG_PAYLOAD_DIR}/bin/rg"

rm -f "${TUIDE_RG_TAR_PATH}"
tar -cf "${TUIDE_RG_TAR_PATH}" -C "${TUIDE_RG_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_RG_TAR_PATH}" -o "${TUIDE_RG_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_RG_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_RG_PAYLOAD_DIR}/bin/rg" | awk '{print $1}')"

cat > "${TUIDE_RG_MANIFEST_PATH}" <<EOF
{
  "version": "${TUIDE_RG_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}"
}
EOF

cat > "${TUIDE_RG_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_RG_VERSION "${TUIDE_RG_VERSION}"
#define TUIDE_BUNDLED_RG_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_RG_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_RG_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_RG_ZST_PATH}")" "$(basename "${TUIDE_RG_BLOB_OBJ}")"
)

printf 'rg bundle listo: %s (%s bytes)\n' "${TUIDE_RG_ZST_PATH}" "$(wc -c < "${TUIDE_RG_ZST_PATH}")"
