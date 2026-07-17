#!/usr/bin/env bash
# Empaqueta typescript-language-server + typescript + Node portable (TGDB_BUNDLE_TSSERVER).
set -euo pipefail

die() { printf 'prepare_typescript_ls_bundle: error: %s\n' "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_TYPESCRIPT_LS_VERSION
require_var TGDB_TYPESCRIPT_LS_NPM_VERSION
require_var TGDB_TYPESCRIPT_VERSION
require_var TGDB_NODE_URL
require_var TGDB_NODE_TAR_PATH
require_var TGDB_TYPESCRIPT_LS_STAGING_DIR
require_var TGDB_TYPESCRIPT_LS_PAYLOAD_DIR
require_var TGDB_TYPESCRIPT_LS_TAR_PATH
require_var TGDB_TYPESCRIPT_LS_ZST_PATH
require_var TGDB_TYPESCRIPT_LS_MANIFEST_HPP
require_var TGDB_TYPESCRIPT_LS_BLOB_OBJ

TGDB_TYPESCRIPT_LS_VERSION="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_VERSION}")"
TGDB_TYPESCRIPT_LS_NPM_VERSION="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_NPM_VERSION}")"
TGDB_TYPESCRIPT_VERSION="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_VERSION}")"
TGDB_NODE_URL="$(strip_cmake_quotes "${TGDB_NODE_URL}")"
TGDB_NODE_TAR_PATH="$(strip_cmake_quotes "${TGDB_NODE_TAR_PATH}")"
TGDB_TYPESCRIPT_LS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_STAGING_DIR}")"
TGDB_TYPESCRIPT_LS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}")"
TGDB_TYPESCRIPT_LS_TAR_PATH="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_TAR_PATH}")"
TGDB_TYPESCRIPT_LS_ZST_PATH="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_ZST_PATH}")"
TGDB_TYPESCRIPT_LS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_MANIFEST_HPP}")"
TGDB_TYPESCRIPT_LS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_TYPESCRIPT_LS_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar npm; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

if [[ ! -f "${TGDB_NODE_TAR_PATH}" ]]; then
  mkdir -p "$(dirname "${TGDB_NODE_TAR_PATH}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${TGDB_NODE_TAR_PATH}" "${TGDB_NODE_URL}"
  else
    wget -O "${TGDB_NODE_TAR_PATH}" "${TGDB_NODE_URL}"
  fi
fi

rm -rf "${TGDB_TYPESCRIPT_LS_STAGING_DIR}" "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}"
mkdir -p "${TGDB_TYPESCRIPT_LS_STAGING_DIR}/node" "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}"
tar -xJf "${TGDB_NODE_TAR_PATH}" -C "${TGDB_TYPESCRIPT_LS_STAGING_DIR}/node" --strip-components=1
cp -a "${TGDB_TYPESCRIPT_LS_STAGING_DIR}/node/." "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/"
NODE_BIN="${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/bin/node"
NPM_BIN="${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/bin/npm"
chmod +x "${NODE_BIN}" "${NPM_BIN}" || true

"${NPM_BIN}" install --prefix "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}" \
  "typescript-language-server@${TGDB_TYPESCRIPT_LS_NPM_VERSION}" \
  "typescript@${TGDB_TYPESCRIPT_VERSION}"

CLI_ENTRY=""
for candidate in \
  "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/node_modules/typescript-language-server/lib/cli.mjs" \
  "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/node_modules/typescript-language-server/lib/cli.js"; do
  if [[ -f "${candidate}" ]]; then
    CLI_ENTRY="${candidate#${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/}"
    break
  fi
done
[[ -n "${CLI_ENTRY}" ]] || die "falta cli.mjs/cli.js tras npm install"

cat > "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/bin/typescript-language-server" <<EOF
#!/bin/sh
ROOT="\$(CDPATH= cd -- "\$(dirname "\$0")/.." && pwd)"
exec "\$ROOT/bin/node" "\$ROOT/${CLI_ENTRY}" "\$@"
EOF
chmod +x "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/bin/typescript-language-server"

rm -f "${TGDB_TYPESCRIPT_LS_TAR_PATH}"
tar -cf "${TGDB_TYPESCRIPT_LS_TAR_PATH}" -C "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_TYPESCRIPT_LS_TAR_PATH}" -o "${TGDB_TYPESCRIPT_LS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_TYPESCRIPT_LS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_TYPESCRIPT_LS_PAYLOAD_DIR}/bin/typescript-language-server" | awk '{print $1}')"

cat > "${TGDB_TYPESCRIPT_LS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_TYPESCRIPT_LS_VERSION "${TGDB_TYPESCRIPT_LS_VERSION}"
#define TGDB_BUNDLED_TYPESCRIPT_LS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_TYPESCRIPT_LS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_TYPESCRIPT_LS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_TYPESCRIPT_LS_ZST_PATH}")" "$(basename "${TGDB_TYPESCRIPT_LS_BLOB_OBJ}")"
)

printf 'typescript-language-server bundle listo: %s (%s bytes)\n' \
  "${TGDB_TYPESCRIPT_LS_ZST_PATH}" "$(wc -c < "${TGDB_TYPESCRIPT_LS_ZST_PATH}")"
