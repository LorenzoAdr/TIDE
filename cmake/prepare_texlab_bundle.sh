#!/usr/bin/env bash
# Empaqueta TexLab Linux x86_64 para TGDB_BUNDLE_TEXLAB.
set -euo pipefail

die() { printf 'prepare_texlab_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_TEXLAB_VERSION
require_var TGDB_TEXLAB_URL
require_var TGDB_TEXLAB_TAR_PATH
require_var TGDB_TEXLAB_STAGING_DIR
require_var TGDB_TEXLAB_PAYLOAD_DIR
require_var TGDB_TEXLAB_TAR_PATH_OUT
require_var TGDB_TEXLAB_ZST_PATH
require_var TGDB_TEXLAB_MANIFEST_PATH
require_var TGDB_TEXLAB_MANIFEST_HPP
require_var TGDB_TEXLAB_BLOB_OBJ

TGDB_TEXLAB_VERSION="$(strip_cmake_quotes "${TGDB_TEXLAB_VERSION}")"
TGDB_TEXLAB_URL="$(strip_cmake_quotes "${TGDB_TEXLAB_URL}")"
TGDB_TEXLAB_TAR_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_TAR_PATH}")"
TGDB_TEXLAB_STAGING_DIR="$(strip_cmake_quotes "${TGDB_TEXLAB_STAGING_DIR}")"
TGDB_TEXLAB_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_TEXLAB_PAYLOAD_DIR}")"
TGDB_TEXLAB_TAR_PATH_OUT="$(strip_cmake_quotes "${TGDB_TEXLAB_TAR_PATH_OUT}")"
TGDB_TEXLAB_ZST_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_ZST_PATH}")"
TGDB_TEXLAB_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_MANIFEST_PATH}")"
TGDB_TEXLAB_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_TEXLAB_MANIFEST_HPP}")"
TGDB_TEXLAB_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_TEXLAB_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

if [[ ! -f "${TGDB_TEXLAB_TAR_PATH}" ]]; then
  mkdir -p "$(dirname "${TGDB_TEXLAB_TAR_PATH}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${TGDB_TEXLAB_TAR_PATH}" "${TGDB_TEXLAB_URL}"
  else
    wget -O "${TGDB_TEXLAB_TAR_PATH}" "${TGDB_TEXLAB_URL}"
  fi
fi

rm -rf "${TGDB_TEXLAB_STAGING_DIR}" "${TGDB_TEXLAB_PAYLOAD_DIR}"
mkdir -p "${TGDB_TEXLAB_STAGING_DIR}" "${TGDB_TEXLAB_PAYLOAD_DIR}/bin"
tar -xzf "${TGDB_TEXLAB_TAR_PATH}" -C "${TGDB_TEXLAB_STAGING_DIR}"
texlab_bin="$(find "${TGDB_TEXLAB_STAGING_DIR}" -type f -name texlab | head -n1)"
[[ -n "${texlab_bin}" ]] || die "no se encontró texlab en el tarball"
cp -a "${texlab_bin}" "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab"
chmod +x "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab"

rm -f "${TGDB_TEXLAB_TAR_PATH_OUT}"
tar -cf "${TGDB_TEXLAB_TAR_PATH_OUT}" -C "${TGDB_TEXLAB_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_TEXLAB_TAR_PATH_OUT}" -o "${TGDB_TEXLAB_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_TEXLAB_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab" | awk '{print $1}')"

cat > "${TGDB_TEXLAB_MANIFEST_PATH}" <<EOF
{"version":"${TGDB_TEXLAB_VERSION}","blob_sha256":"${blob_sha}","binary_sha256":"${bin_sha}"}
EOF

cat > "${TGDB_TEXLAB_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_TEXLAB_VERSION "${TGDB_TEXLAB_VERSION}"
#define TGDB_BUNDLED_TEXLAB_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_TEXLAB_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_TEXLAB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_TEXLAB_ZST_PATH}")" "$(basename "${TGDB_TEXLAB_BLOB_OBJ}")"
)

printf 'texlab bundle listo: %s (%s bytes)\n' \
  "${TGDB_TEXLAB_ZST_PATH}" "$(wc -c < "${TGDB_TEXLAB_ZST_PATH}")"
