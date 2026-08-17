#!/usr/bin/env bash
# Empaqueta un binario (o árbol tar completo) para blobs TUIDE embebidos.
# Modos TUIDE_EXTRACT_MODE:
#   gunzip      — descarga .gz → payload/bin/$TUIDE_BINARY_NAME
#   tar_binary  — tar.gz/.tar.xz → busca $TUIDE_BINARY_NAME → payload/bin/
#   zip_binary  — .zip → busca $TUIDE_ARCHIVE_BINARY_NAME (o BINARY_NAME) → payload/bin/$TUIDE_BINARY_NAME
#   tar_tree    — extrae el tarball completo en payload/ (p. ej. lua-language-server)
set -euo pipefail

die() { printf 'prepare_single_bin_bundle [%s]: error: %s\n' "${TUIDE_TOOL_ID:-?}" "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TUIDE_TOOL_ID
require_var TUIDE_TOOL_VERSION
require_var TUIDE_EXTRACT_MODE
require_var TUIDE_BINARY_NAME
require_var TUIDE_DOWNLOAD_URL
require_var TUIDE_DOWNLOAD_PATH
require_var TUIDE_STAGING_DIR
require_var TUIDE_PAYLOAD_DIR
require_var TUIDE_TAR_PATH_OUT
require_var TUIDE_ZST_PATH
require_var TUIDE_MANIFEST_HPP
require_var TUIDE_BLOB_OBJ
require_var TUIDE_MANIFEST_PREFIX

TUIDE_TOOL_ID="$(strip_cmake_quotes "${TUIDE_TOOL_ID}")"
TUIDE_TOOL_VERSION="$(strip_cmake_quotes "${TUIDE_TOOL_VERSION}")"
TUIDE_EXTRACT_MODE="$(strip_cmake_quotes "${TUIDE_EXTRACT_MODE}")"
TUIDE_BINARY_NAME="$(strip_cmake_quotes "${TUIDE_BINARY_NAME}")"
TUIDE_DOWNLOAD_URL="$(strip_cmake_quotes "${TUIDE_DOWNLOAD_URL}")"
TUIDE_DOWNLOAD_PATH="$(strip_cmake_quotes "${TUIDE_DOWNLOAD_PATH}")"
TUIDE_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_STAGING_DIR}")"
TUIDE_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_PAYLOAD_DIR}")"
TUIDE_TAR_PATH_OUT="$(strip_cmake_quotes "${TUIDE_TAR_PATH_OUT}")"
TUIDE_ZST_PATH="$(strip_cmake_quotes "${TUIDE_ZST_PATH}")"
TUIDE_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_MANIFEST_HPP}")"
TUIDE_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_BLOB_OBJ}")"
TUIDE_MANIFEST_PREFIX="$(strip_cmake_quotes "${TUIDE_MANIFEST_PREFIX}")"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done
ARCHIVE_BINARY_NAME="${TUIDE_ARCHIVE_BINARY_NAME:-${TUIDE_BINARY_NAME}}"
ARCHIVE_BINARY_NAME="$(strip_cmake_quotes "${ARCHIVE_BINARY_NAME}")"

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

if [[ ! -f "${TUIDE_DOWNLOAD_PATH}" ]]; then
  download_file "${TUIDE_DOWNLOAD_URL}" "${TUIDE_DOWNLOAD_PATH}"
fi

rm -rf "${TUIDE_STAGING_DIR}" "${TUIDE_PAYLOAD_DIR}"
mkdir -p "${TUIDE_STAGING_DIR}" "${TUIDE_PAYLOAD_DIR}/bin"

case "${TUIDE_EXTRACT_MODE}" in
  gunzip)
    gunzip -c "${TUIDE_DOWNLOAD_PATH}" > "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    chmod +x "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    ;;
  tar_binary)
    extract_tar "${TUIDE_DOWNLOAD_PATH}" "${TUIDE_STAGING_DIR}"
    found_bin="$(find "${TUIDE_STAGING_DIR}" -type f -name "${TUIDE_BINARY_NAME}" | head -n1)"
    [[ -n "${found_bin}" && -f "${found_bin}" ]] \
      || die "no se encontró ${TUIDE_BINARY_NAME} en el archivo"
    cp -a "${found_bin}" "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    chmod +x "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    ;;
  zip_binary)
    command -v unzip >/dev/null || die "falta unzip"
    unzip -q "${TUIDE_DOWNLOAD_PATH}" -d "${TUIDE_STAGING_DIR}"
    found_bin="$(find "${TUIDE_STAGING_DIR}" -type f -name "${ARCHIVE_BINARY_NAME}" | head -n1)"
    if [[ -z "${found_bin}" && "${ARCHIVE_BINARY_NAME}" != "${TUIDE_BINARY_NAME}" ]]; then
      found_bin="$(find "${TUIDE_STAGING_DIR}" -type f -name "${TUIDE_BINARY_NAME}" | head -n1)"
    fi
    [[ -n "${found_bin}" && -f "${found_bin}" ]] \
      || die "no se encontró ${ARCHIVE_BINARY_NAME} en el zip"
    cp -a "${found_bin}" "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    chmod +x "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}"
    ;;
  tar_tree)
    extract_tar "${TUIDE_DOWNLOAD_PATH}" "${TUIDE_PAYLOAD_DIR}"
    [[ -x "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}" ]] \
      || die "falta bin/${TUIDE_BINARY_NAME} tras extraer el tarball"
    ;;
  *)
    die "TUIDE_EXTRACT_MODE desconocido: ${TUIDE_EXTRACT_MODE}"
    ;;
esac

rm -f "${TUIDE_TAR_PATH_OUT}"
tar -cf "${TUIDE_TAR_PATH_OUT}" -C "${TUIDE_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_TAR_PATH_OUT}" -o "${TUIDE_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_PAYLOAD_DIR}/bin/${TUIDE_BINARY_NAME}" | awk '{print $1}')"

cat > "${TUIDE_MANIFEST_HPP}" <<EOF
#pragma once
#define ${TUIDE_MANIFEST_PREFIX}_VERSION "${TUIDE_TOOL_VERSION}"
#define ${TUIDE_MANIFEST_PREFIX}_BLOB_SHA256 "${blob_sha}"
#define ${TUIDE_MANIFEST_PREFIX}_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_ZST_PATH}")" "$(basename "${TUIDE_BLOB_OBJ}")"
)

printf '%s bundle listo: %s (%s bytes)\n' \
  "${TUIDE_TOOL_ID}" "${TUIDE_ZST_PATH}" "$(wc -c < "${TUIDE_ZST_PATH}")"
