#!/usr/bin/env bash
# Empaqueta vscode-bash-debug (out/) + bashdb_dir (+ Node portable).
set -euo pipefail

die() { printf 'prepare_bash_dap_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"; value="${value#\"}"; value="${value%\"}"; printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TUIDE_BASH_DAP_VERSION
require_var TUIDE_BASH_DEBUG_SRC
require_var TUIDE_NODE_URL
require_var TUIDE_NODE_TAR_PATH
require_var TUIDE_BASH_DAP_STAGING_DIR
require_var TUIDE_BASH_DAP_PAYLOAD_DIR
require_var TUIDE_BASH_DAP_TAR_PATH
require_var TUIDE_BASH_DAP_ZST_PATH
require_var TUIDE_BASH_DAP_MANIFEST_PATH
require_var TUIDE_BASH_DAP_MANIFEST_HPP
require_var TUIDE_BASH_DAP_BLOB_OBJ
TUIDE_BASH_DAP_INCLUDE_NODE="${TUIDE_BASH_DAP_INCLUDE_NODE:-1}"

TUIDE_BASH_DAP_VERSION="$(strip_cmake_quotes "${TUIDE_BASH_DAP_VERSION}")"
TUIDE_BASH_DEBUG_SRC="$(strip_cmake_quotes "${TUIDE_BASH_DEBUG_SRC}")"
TUIDE_NODE_URL="$(strip_cmake_quotes "${TUIDE_NODE_URL}")"
TUIDE_NODE_TAR_PATH="$(strip_cmake_quotes "${TUIDE_NODE_TAR_PATH}")"
TUIDE_BASH_DAP_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_BASH_DAP_STAGING_DIR}")"
TUIDE_BASH_DAP_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_BASH_DAP_PAYLOAD_DIR}")"
TUIDE_BASH_DAP_TAR_PATH="$(strip_cmake_quotes "${TUIDE_BASH_DAP_TAR_PATH}")"
TUIDE_BASH_DAP_ZST_PATH="$(strip_cmake_quotes "${TUIDE_BASH_DAP_ZST_PATH}")"
TUIDE_BASH_DAP_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_BASH_DAP_MANIFEST_PATH}")"
TUIDE_BASH_DAP_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_BASH_DAP_MANIFEST_HPP}")"
TUIDE_BASH_DAP_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_BASH_DAP_BLOB_OBJ}")"

[[ -f "${TUIDE_BASH_DEBUG_SRC}/out/bashDebug.js" ]] || die "falta ${TUIDE_BASH_DEBUG_SRC}/out/bashDebug.js (npm run compile)"
[[ -x "${TUIDE_BASH_DEBUG_SRC}/bashdb_dir/bashdb" ]] || die "falta bashdb_dir/bashdb"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

rm -rf "${TUIDE_BASH_DAP_STAGING_DIR}" "${TUIDE_BASH_DAP_PAYLOAD_DIR}"
mkdir -p "${TUIDE_BASH_DAP_PAYLOAD_DIR}/adapter" "${TUIDE_BASH_DAP_PAYLOAD_DIR}/bashdb"

cp -a "${TUIDE_BASH_DEBUG_SRC}/out/." "${TUIDE_BASH_DAP_PAYLOAD_DIR}/adapter/"
cp -aL "${TUIDE_BASH_DEBUG_SRC}/bashdb_dir/." "${TUIDE_BASH_DAP_PAYLOAD_DIR}/bashdb/"
chmod +x "${TUIDE_BASH_DAP_PAYLOAD_DIR}/bashdb/bashdb" || true

# bashDebug.js requires vscode-debugadapter / shell-quote / … at runtime. Without
# these next to the script, node exits immediately and DAP initialize hangs forever.
src_nm="${TUIDE_BASH_DEBUG_SRC}/node_modules"
dst_nm="${TUIDE_BASH_DAP_PAYLOAD_DIR}/adapter/node_modules"
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

if [[ "${TUIDE_BASH_DAP_INCLUDE_NODE}" == "1" ]]; then
  if [[ ! -f "${TUIDE_NODE_TAR_PATH}" ]]; then
    mkdir -p "$(dirname "${TUIDE_NODE_TAR_PATH}")"
    if command -v curl >/dev/null; then
      curl -fL --retry 3 -o "${TUIDE_NODE_TAR_PATH}" "${TUIDE_NODE_URL}"
    else
      wget -O "${TUIDE_NODE_TAR_PATH}" "${TUIDE_NODE_URL}"
    fi
  fi
  mkdir -p "${TUIDE_BASH_DAP_STAGING_DIR}/node"
  tar -xJf "${TUIDE_NODE_TAR_PATH}" -C "${TUIDE_BASH_DAP_STAGING_DIR}/node" --strip-components=1
  mkdir -p "${TUIDE_BASH_DAP_PAYLOAD_DIR}/node/bin"
  cp -a "${TUIDE_BASH_DAP_STAGING_DIR}/node/bin/node" "${TUIDE_BASH_DAP_PAYLOAD_DIR}/node/bin/node"
  chmod +x "${TUIDE_BASH_DAP_PAYLOAD_DIR}/node/bin/node"
fi

rm -f "${TUIDE_BASH_DAP_TAR_PATH}"
tar -cf "${TUIDE_BASH_DAP_TAR_PATH}" -C "${TUIDE_BASH_DAP_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_BASH_DAP_TAR_PATH}" -o "${TUIDE_BASH_DAP_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_BASH_DAP_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_BASH_DAP_PAYLOAD_DIR}/adapter/bashDebug.js" | awk '{print $1}')"
include_node=0
[[ "${TUIDE_BASH_DAP_INCLUDE_NODE}" == "1" ]] && include_node=1

cat > "${TUIDE_BASH_DAP_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_BASH_DAP_VERSION "${TUIDE_BASH_DAP_VERSION}"
#define TUIDE_BUNDLED_BASH_DAP_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_BASH_DAP_BINARY_SHA256 "${bin_sha}"
#define TUIDE_BUNDLED_BASH_DAP_HAS_NODE ${include_node}
EOF

blob_dir="$(dirname "${TUIDE_BASH_DAP_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_BASH_DAP_ZST_PATH}")" "$(basename "${TUIDE_BASH_DAP_BLOB_OBJ}")"
)

printf 'bash dap bundle listo: %s (%s bytes)\n' \
  "${TUIDE_BASH_DAP_ZST_PATH}" "$(wc -c < "${TUIDE_BASH_DAP_ZST_PATH}")"
