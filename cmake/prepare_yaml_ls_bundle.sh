#!/usr/bin/env bash
# Empaqueta yaml-language-server + Node portable (TUIDE_BUNDLE_YAML_LS).
set -euo pipefail

die() { printf 'prepare_yaml_ls_bundle: error: %s\n' "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TUIDE_YAML_LS_VERSION
require_var TUIDE_YAML_LS_NPM_VERSION
require_var TUIDE_NODE_URL
require_var TUIDE_NODE_TAR_PATH
require_var TUIDE_YAML_LS_STAGING_DIR
require_var TUIDE_YAML_LS_PAYLOAD_DIR
require_var TUIDE_YAML_LS_TAR_PATH
require_var TUIDE_YAML_LS_ZST_PATH
require_var TUIDE_YAML_LS_MANIFEST_HPP
require_var TUIDE_YAML_LS_BLOB_OBJ

TUIDE_YAML_LS_VERSION="$(strip_cmake_quotes "${TUIDE_YAML_LS_VERSION}")"
TUIDE_YAML_LS_NPM_VERSION="$(strip_cmake_quotes "${TUIDE_YAML_LS_NPM_VERSION}")"
TUIDE_NODE_URL="$(strip_cmake_quotes "${TUIDE_NODE_URL}")"
TUIDE_NODE_TAR_PATH="$(strip_cmake_quotes "${TUIDE_NODE_TAR_PATH}")"
TUIDE_YAML_LS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_YAML_LS_STAGING_DIR}")"
TUIDE_YAML_LS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_YAML_LS_PAYLOAD_DIR}")"
TUIDE_YAML_LS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_YAML_LS_TAR_PATH}")"
TUIDE_YAML_LS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_YAML_LS_ZST_PATH}")"
TUIDE_YAML_LS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_YAML_LS_MANIFEST_HPP}")"
TUIDE_YAML_LS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_YAML_LS_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar npm; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

if [[ ! -f "${TUIDE_NODE_TAR_PATH}" ]]; then
  mkdir -p "$(dirname "${TUIDE_NODE_TAR_PATH}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${TUIDE_NODE_TAR_PATH}" "${TUIDE_NODE_URL}"
  else
    wget -O "${TUIDE_NODE_TAR_PATH}" "${TUIDE_NODE_URL}"
  fi
fi

rm -rf "${TUIDE_YAML_LS_STAGING_DIR}" "${TUIDE_YAML_LS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_YAML_LS_STAGING_DIR}/node" "${TUIDE_YAML_LS_PAYLOAD_DIR}"
tar -xJf "${TUIDE_NODE_TAR_PATH}" -C "${TUIDE_YAML_LS_STAGING_DIR}/node" --strip-components=1
cp -a "${TUIDE_YAML_LS_STAGING_DIR}/node/." "${TUIDE_YAML_LS_PAYLOAD_DIR}/"
NODE_BIN="${TUIDE_YAML_LS_PAYLOAD_DIR}/bin/node"
NPM_BIN="${TUIDE_YAML_LS_PAYLOAD_DIR}/bin/npm"
chmod +x "${NODE_BIN}" "${NPM_BIN}" || true

"${NPM_BIN}" install --prefix "${TUIDE_YAML_LS_PAYLOAD_DIR}" \
  "yaml-language-server@${TUIDE_YAML_LS_NPM_VERSION}"

CLI_ENTRY=""
for candidate in \
  "${TUIDE_YAML_LS_PAYLOAD_DIR}/node_modules/yaml-language-server/bin/yaml-language-server" \
  "${TUIDE_YAML_LS_PAYLOAD_DIR}/node_modules/yaml-language-server/out/server/src/index.js"; do
  if [[ -f "${candidate}" ]]; then
    CLI_ENTRY="${candidate#${TUIDE_YAML_LS_PAYLOAD_DIR}/}"
    break
  fi
done
[[ -n "${CLI_ENTRY}" ]] || die "falta bin/yaml-language-server tras npm install"

cat > "${TUIDE_YAML_LS_PAYLOAD_DIR}/bin/yaml-language-server" <<EOF
#!/bin/sh
ROOT="\$(CDPATH= cd -- "\$(dirname "\$0")/.." && pwd)"
exec "\$ROOT/bin/node" "\$ROOT/${CLI_ENTRY}" "\$@"
EOF
chmod +x "${TUIDE_YAML_LS_PAYLOAD_DIR}/bin/yaml-language-server"

rm -f "${TUIDE_YAML_LS_TAR_PATH}"
tar -cf "${TUIDE_YAML_LS_TAR_PATH}" -C "${TUIDE_YAML_LS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_YAML_LS_TAR_PATH}" -o "${TUIDE_YAML_LS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_YAML_LS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_YAML_LS_PAYLOAD_DIR}/bin/yaml-language-server" | awk '{print $1}')"

cat > "${TUIDE_YAML_LS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_YAML_LS_VERSION "${TUIDE_YAML_LS_VERSION}"
#define TUIDE_BUNDLED_YAML_LS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_YAML_LS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_YAML_LS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_YAML_LS_ZST_PATH}")" "$(basename "${TUIDE_YAML_LS_BLOB_OBJ}")"
)

printf 'yaml-language-server bundle listo: %s (%s bytes)\n' \
  "${TUIDE_YAML_LS_ZST_PATH}" "$(wc -c < "${TUIDE_YAML_LS_ZST_PATH}")"
