#!/usr/bin/env bash
# Empaqueta un binario (o árbol tar completo) para blobs TGDB embebidos.
# Modos TGDB_EXTRACT_MODE:
#   gunzip      — descarga .gz → payload/bin/$TGDB_BINARY_NAME
#   tar_binary  — tar.gz/.tar.xz → busca $TGDB_BINARY_NAME → payload/bin/
#   tar_tree    — extrae el tarball completo en payload/ (p. ej. lua-language-server)
set -euo pipefail

die() { printf 'prepare_single_bin_bundle [%s]: error: %s\n' "${TGDB_TOOL_ID:-?}" "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_TOOL_ID
require_var TGDB_TOOL_VERSION
require_var TGDB_EXTRACT_MODE
require_var TGDB_BINARY_NAME
require_var TGDB_DOWNLOAD_URL
require_var TGDB_DOWNLOAD_PATH
require_var TGDB_STAGING_DIR
require_var TGDB_PAYLOAD_DIR
require_var TGDB_TAR_PATH_OUT
require_var TGDB_ZST_PATH
require_var TGDB_MANIFEST_HPP
require_var TGDB_BLOB_OBJ
require_var TGDB_MANIFEST_PREFIX

TGDB_TOOL_ID="$(strip_cmake_quotes "${TGDB_TOOL_ID}")"
TGDB_TOOL_VERSION="$(strip_cmake_quotes "${TGDB_TOOL_VERSION}")"
TGDB_EXTRACT_MODE="$(strip_cmake_quotes "${TGDB_EXTRACT_MODE}")"
TGDB_BINARY_NAME="$(strip_cmake_quotes "${TGDB_BINARY_NAME}")"
TGDB_DOWNLOAD_URL="$(strip_cmake_quotes "${TGDB_DOWNLOAD_URL}")"
TGDB_DOWNLOAD_PATH="$(strip_cmake_quotes "${TGDB_DOWNLOAD_PATH}")"
TGDB_STAGING_DIR="$(strip_cmake_quotes "${TGDB_STAGING_DIR}")"
TGDB_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_PAYLOAD_DIR}")"
TGDB_TAR_PATH_OUT="$(strip_cmake_quotes "${TGDB_TAR_PATH_OUT}")"
TGDB_ZST_PATH="$(strip_cmake_quotes "${TGDB_ZST_PATH}")"
TGDB_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_MANIFEST_HPP}")"
TGDB_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_BLOB_OBJ}")"
TGDB_MANIFEST_PREFIX="$(strip_cmake_quotes "${TGDB_MANIFEST_PREFIX}")"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

download_file() {
  local url="$1"
  local dest="$2"
  mkdir -p "$(dirname "${dest}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${dest}" "${url}"
  else
    wget -O "${dest}" "${url}"
  fi
}

extract_tar() {
  local archive="$1"
  local dest="$2"
  case "${archive}" in
    *.tar.gz|*.tgz) tar -xzf "${archive}" -C "${dest}" ;;
    *.tar.xz|*.txz) tar -xJf "${archive}" -C "${dest}" ;;
    *.tar) tar -xf "${archive}" -C "${dest}" ;;
    *) die "formato tar no soportado: ${archive}" ;;
  esac
}

if [[ ! -f "${TGDB_DOWNLOAD_PATH}" ]]; then
  download_file "${TGDB_DOWNLOAD_URL}" "${TGDB_DOWNLOAD_PATH}"
fi

rm -rf "${TGDB_STAGING_DIR}" "${TGDB_PAYLOAD_DIR}"
mkdir -p "${TGDB_STAGING_DIR}" "${TGDB_PAYLOAD_DIR}/bin"

case "${TGDB_EXTRACT_MODE}" in
  gunzip)
    gunzip -c "${TGDB_DOWNLOAD_PATH}" > "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}"
    chmod +x "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}"
    ;;
  tar_binary)
    extract_tar "${TGDB_DOWNLOAD_PATH}" "${TGDB_STAGING_DIR}"
    found_bin="$(find "${TGDB_STAGING_DIR}" -type f -name "${TGDB_BINARY_NAME}" | head -n1)"
    [[ -n "${found_bin}" && -f "${found_bin}" ]] \
      || die "no se encontró ${TGDB_BINARY_NAME} en el archivo"
    cp -a "${found_bin}" "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}"
    chmod +x "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}"
    ;;
  tar_tree)
    extract_tar "${TGDB_DOWNLOAD_PATH}" "${TGDB_PAYLOAD_DIR}"
    [[ -x "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}" ]] \
      || die "falta bin/${TGDB_BINARY_NAME} tras extraer el tarball"
    ;;
  *)
    die "TGDB_EXTRACT_MODE desconocido: ${TGDB_EXTRACT_MODE}"
    ;;
esac

rm -f "${TGDB_TAR_PATH_OUT}"
tar -cf "${TGDB_TAR_PATH_OUT}" -C "${TGDB_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_TAR_PATH_OUT}" -o "${TGDB_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_PAYLOAD_DIR}/bin/${TGDB_BINARY_NAME}" | awk '{print $1}')"

cat > "${TGDB_MANIFEST_HPP}" <<EOF
#pragma once
#define ${TGDB_MANIFEST_PREFIX}_VERSION "${TGDB_TOOL_VERSION}"
#define ${TGDB_MANIFEST_PREFIX}_BLOB_SHA256 "${blob_sha}"
#define ${TGDB_MANIFEST_PREFIX}_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_ZST_PATH}")" "$(basename "${TGDB_BLOB_OBJ}")"
)

printf '%s bundle listo: %s (%s bytes)\n' \
  "${TGDB_TOOL_ID}" "${TGDB_ZST_PATH}" "$(wc -c < "${TGDB_ZST_PATH}")"
