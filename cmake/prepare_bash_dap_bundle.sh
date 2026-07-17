#!/usr/bin/env bash
# Empaqueta vscode-bash-debug (out/) + bashdb_dir (+ Node portable).
set -euo pipefail

die() { printf 'prepare_bash_dap_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"; value="${value#\"}"; value="${value%\"}"; printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_BASH_DAP_VERSION
require_var TGDB_BASH_DEBUG_SRC
require_var TGDB_NODE_URL
require_var TGDB_NODE_TAR_PATH
require_var TGDB_BASH_DAP_STAGING_DIR
require_var TGDB_BASH_DAP_PAYLOAD_DIR
require_var TGDB_BASH_DAP_TAR_PATH
require_var TGDB_BASH_DAP_ZST_PATH
require_var TGDB_BASH_DAP_MANIFEST_PATH
require_var TGDB_BASH_DAP_MANIFEST_HPP
require_var TGDB_BASH_DAP_BLOB_OBJ
TGDB_BASH_DAP_INCLUDE_NODE="${TGDB_BASH_DAP_INCLUDE_NODE:-1}"

TGDB_BASH_DAP_VERSION="$(strip_cmake_quotes "${TGDB_BASH_DAP_VERSION}")"
TGDB_BASH_DEBUG_SRC="$(strip_cmake_quotes "${TGDB_BASH_DEBUG_SRC}")"
TGDB_NODE_URL="$(strip_cmake_quotes "${TGDB_NODE_URL}")"
TGDB_NODE_TAR_PATH="$(strip_cmake_quotes "${TGDB_NODE_TAR_PATH}")"
TGDB_BASH_DAP_STAGING_DIR="$(strip_cmake_quotes "${TGDB_BASH_DAP_STAGING_DIR}")"
TGDB_BASH_DAP_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_BASH_DAP_PAYLOAD_DIR}")"
TGDB_BASH_DAP_TAR_PATH="$(strip_cmake_quotes "${TGDB_BASH_DAP_TAR_PATH}")"
TGDB_BASH_DAP_ZST_PATH="$(strip_cmake_quotes "${TGDB_BASH_DAP_ZST_PATH}")"
TGDB_BASH_DAP_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_BASH_DAP_MANIFEST_PATH}")"
TGDB_BASH_DAP_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_BASH_DAP_MANIFEST_HPP}")"
TGDB_BASH_DAP_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_BASH_DAP_BLOB_OBJ}")"

[[ -f "${TGDB_BASH_DEBUG_SRC}/out/bashDebug.js" ]] || die "falta ${TGDB_BASH_DEBUG_SRC}/out/bashDebug.js (npm run compile)"
[[ -x "${TGDB_BASH_DEBUG_SRC}/bashdb_dir/bashdb" ]] || die "falta bashdb_dir/bashdb"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

rm -rf "${TGDB_BASH_DAP_STAGING_DIR}" "${TGDB_BASH_DAP_PAYLOAD_DIR}"
mkdir -p "${TGDB_BASH_DAP_PAYLOAD_DIR}/adapter" "${TGDB_BASH_DAP_PAYLOAD_DIR}/bashdb"

cp -a "${TGDB_BASH_DEBUG_SRC}/out/." "${TGDB_BASH_DAP_PAYLOAD_DIR}/adapter/"
cp -aL "${TGDB_BASH_DEBUG_SRC}/bashdb_dir/." "${TGDB_BASH_DAP_PAYLOAD_DIR}/bashdb/"
chmod +x "${TGDB_BASH_DAP_PAYLOAD_DIR}/bashdb/bashdb" || true

# bashDebug.js requires vscode-debugadapter / shell-quote / … at runtime. Without
# these next to the script, node exits immediately and DAP initialize hangs forever.
src_nm="${TGDB_BASH_DEBUG_SRC}/node_modules"
dst_nm="${TGDB_BASH_DAP_PAYLOAD_DIR}/adapter/node_modules"
[[ -d "${src_nm}" ]] || die "falta ${src_nm} (cd third_party/bash-debug && npm install)"
mkdir -p "${dst_nm}"
# Production deps + transitive closures needed by out/bashDebug.js
for pkg in \
  vscode-debugadapter vscode-debugprotocol shell-quote \
  npm-which child-process which isexe commander minimist mkdirp npm-path
do
  if [[ -d "${src_nm}/${pkg}" ]]; then
    cp -a "${src_nm}/${pkg}" "${dst_nm}/${pkg}"
  else
    die "falta dependencia npm: ${pkg}"
  fi
done

if [[ "${TGDB_BASH_DAP_INCLUDE_NODE}" == "1" ]]; then
  if [[ ! -f "${TGDB_NODE_TAR_PATH}" ]]; then
    mkdir -p "$(dirname "${TGDB_NODE_TAR_PATH}")"
    if command -v curl >/dev/null; then
      curl -fL --retry 3 -o "${TGDB_NODE_TAR_PATH}" "${TGDB_NODE_URL}"
    else
      wget -O "${TGDB_NODE_TAR_PATH}" "${TGDB_NODE_URL}"
    fi
  fi
  mkdir -p "${TGDB_BASH_DAP_STAGING_DIR}/node"
  tar -xJf "${TGDB_NODE_TAR_PATH}" -C "${TGDB_BASH_DAP_STAGING_DIR}/node" --strip-components=1
  mkdir -p "${TGDB_BASH_DAP_PAYLOAD_DIR}/node/bin"
  cp -a "${TGDB_BASH_DAP_STAGING_DIR}/node/bin/node" "${TGDB_BASH_DAP_PAYLOAD_DIR}/node/bin/node"
  chmod +x "${TGDB_BASH_DAP_PAYLOAD_DIR}/node/bin/node"
fi

rm -f "${TGDB_BASH_DAP_TAR_PATH}"
tar -cf "${TGDB_BASH_DAP_TAR_PATH}" -C "${TGDB_BASH_DAP_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_BASH_DAP_TAR_PATH}" -o "${TGDB_BASH_DAP_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_BASH_DAP_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_BASH_DAP_PAYLOAD_DIR}/adapter/bashDebug.js" | awk '{print $1}')"
include_node=0
[[ "${TGDB_BASH_DAP_INCLUDE_NODE}" == "1" ]] && include_node=1

cat > "${TGDB_BASH_DAP_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_BASH_DAP_VERSION "${TGDB_BASH_DAP_VERSION}"
#define TGDB_BUNDLED_BASH_DAP_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_BASH_DAP_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_BASH_DAP_HAS_NODE ${include_node}
EOF

blob_dir="$(dirname "${TGDB_BASH_DAP_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_BASH_DAP_ZST_PATH}")" "$(basename "${TGDB_BASH_DAP_BLOB_OBJ}")"
)

printf 'bash dap bundle listo: %s (%s bytes)\n' \
  "${TGDB_BASH_DAP_ZST_PATH}" "$(wc -c < "${TGDB_BASH_DAP_ZST_PATH}")"
