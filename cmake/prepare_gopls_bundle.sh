#!/usr/bin/env bash
# Empaqueta gopls vía `go install` (no hay tarball oficial en GitHub releases).
set -euo pipefail

die() { printf 'prepare_gopls_bundle: error: %s\n' "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_GOPLS_VERSION
require_var TGDB_GOPLS_STAGING_DIR
require_var TGDB_GOPLS_PAYLOAD_DIR
require_var TGDB_GOPLS_TAR_PATH
require_var TGDB_GOPLS_ZST_PATH
require_var TGDB_GOPLS_MANIFEST_HPP
require_var TGDB_GOPLS_BLOB_OBJ

TGDB_GOPLS_VERSION="$(strip_cmake_quotes "${TGDB_GOPLS_VERSION}")"
TGDB_GOPLS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_GOPLS_STAGING_DIR}")"
TGDB_GOPLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_GOPLS_PAYLOAD_DIR}")"
TGDB_GOPLS_TAR_PATH="$(strip_cmake_quotes "${TGDB_GOPLS_TAR_PATH}")"
TGDB_GOPLS_ZST_PATH="$(strip_cmake_quotes "${TGDB_GOPLS_ZST_PATH}")"
TGDB_GOPLS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_GOPLS_MANIFEST_HPP}")"
TGDB_GOPLS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_GOPLS_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy go; do
  command -v "${tool}" >/dev/null || die "falta ${tool} (gopls requiere Go en PATH en tiempo de bundle)"
done

rm -rf "${TGDB_GOPLS_STAGING_DIR}" "${TGDB_GOPLS_PAYLOAD_DIR}"
mkdir -p "${TGDB_GOPLS_STAGING_DIR}/bin" "${TGDB_GOPLS_PAYLOAD_DIR}/bin"

export GOBIN="${TGDB_GOPLS_STAGING_DIR}/bin"
export GO111MODULE=on
go install "golang.org/x/tools/gopls@${TGDB_GOPLS_VERSION}"

[[ -x "${GOBIN}/gopls" ]] || die "go install no produjo gopls en ${GOBIN}"
cp -a "${GOBIN}/gopls" "${TGDB_GOPLS_PAYLOAD_DIR}/bin/gopls"
chmod +x "${TGDB_GOPLS_PAYLOAD_DIR}/bin/gopls"

rm -f "${TGDB_GOPLS_TAR_PATH}"
tar -cf "${TGDB_GOPLS_TAR_PATH}" -C "${TGDB_GOPLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_GOPLS_TAR_PATH}" -o "${TGDB_GOPLS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_GOPLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_GOPLS_PAYLOAD_DIR}/bin/gopls" | awk '{print $1}')"

cat > "${TGDB_GOPLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_GOPLS_VERSION "${TGDB_GOPLS_VERSION}"
#define TGDB_BUNDLED_GOPLS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_GOPLS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_GOPLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_GOPLS_ZST_PATH}")" "$(basename "${TGDB_GOPLS_BLOB_OBJ}")"
)

printf 'gopls bundle listo: %s (%s bytes)\n' \
  "${TGDB_GOPLS_ZST_PATH}" "$(wc -c < "${TGDB_GOPLS_ZST_PATH}")"
