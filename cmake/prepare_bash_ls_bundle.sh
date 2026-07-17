#!/usr/bin/env bash
# Empaqueta bash-language-server + Node portable (TGDB_BUNDLE_BASH_LS).
set -euo pipefail

die() { printf 'prepare_bash_ls_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"; value="${value#\"}"; value="${value%\"}"; printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_BASH_LS_VERSION
require_var TGDB_BASH_LS_NPM_VERSION
require_var TGDB_NODE_URL
require_var TGDB_NODE_TAR_PATH
require_var TGDB_BASH_LS_STAGING_DIR
require_var TGDB_BASH_LS_PAYLOAD_DIR
require_var TGDB_BASH_LS_TAR_PATH
require_var TGDB_BASH_LS_ZST_PATH
require_var TGDB_BASH_LS_MANIFEST_HPP
require_var TGDB_BASH_LS_BLOB_OBJ

TGDB_BASH_LS_VERSION="$(strip_cmake_quotes "${TGDB_BASH_LS_VERSION}")"
TGDB_BASH_LS_NPM_VERSION="$(strip_cmake_quotes "${TGDB_BASH_LS_NPM_VERSION}")"
TGDB_NODE_URL="$(strip_cmake_quotes "${TGDB_NODE_URL}")"
TGDB_NODE_TAR_PATH="$(strip_cmake_quotes "${TGDB_NODE_TAR_PATH}")"
TGDB_BASH_LS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_BASH_LS_STAGING_DIR}")"
TGDB_BASH_LS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_BASH_LS_PAYLOAD_DIR}")"
TGDB_BASH_LS_TAR_PATH="$(strip_cmake_quotes "${TGDB_BASH_LS_TAR_PATH}")"
TGDB_BASH_LS_ZST_PATH="$(strip_cmake_quotes "${TGDB_BASH_LS_ZST_PATH}")"
TGDB_BASH_LS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_BASH_LS_MANIFEST_HPP}")"
TGDB_BASH_LS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_BASH_LS_BLOB_OBJ}")"

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

rm -rf "${TGDB_BASH_LS_STAGING_DIR}" "${TGDB_BASH_LS_PAYLOAD_DIR}"
mkdir -p "${TGDB_BASH_LS_STAGING_DIR}/node" "${TGDB_BASH_LS_PAYLOAD_DIR}"
tar -xJf "${TGDB_NODE_TAR_PATH}" -C "${TGDB_BASH_LS_STAGING_DIR}/node" --strip-components=1
cp -a "${TGDB_BASH_LS_STAGING_DIR}/node/." "${TGDB_BASH_LS_PAYLOAD_DIR}/"
NODE_BIN="${TGDB_BASH_LS_PAYLOAD_DIR}/bin/node"
NPM_BIN="${TGDB_BASH_LS_PAYLOAD_DIR}/bin/npm"
chmod +x "${NODE_BIN}" "${NPM_BIN}" || true

# Install bash-language-server into the portable node prefix.
"${NPM_BIN}" install --prefix "${TGDB_BASH_LS_PAYLOAD_DIR}" "bash-language-server@${TGDB_BASH_LS_NPM_VERSION}"

# Optional ShellCheck static binary for diagnostics (SCxxxx).
TGDB_BUNDLED_CACHE_DIR="$(dirname "${TGDB_NODE_TAR_PATH}")"
TGDB_SHELLCHECK_VERSION="${TGDB_SHELLCHECK_VERSION:-0.10.0}"
TGDB_SHELLCHECK_TAR_NAME="shellcheck-v${TGDB_SHELLCHECK_VERSION}.linux.x86_64.tar.xz"
TGDB_SHELLCHECK_URL="https://github.com/koalaman/shellcheck/releases/download/v${TGDB_SHELLCHECK_VERSION}/${TGDB_SHELLCHECK_TAR_NAME}"
TGDB_SHELLCHECK_TAR_PATH="${TGDB_BUNDLED_CACHE_DIR}/${TGDB_SHELLCHECK_TAR_NAME}"
if [[ ! -f "${TGDB_SHELLCHECK_TAR_PATH}" ]]; then
  mkdir -p "$(dirname "${TGDB_SHELLCHECK_TAR_PATH}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${TGDB_SHELLCHECK_TAR_PATH}" "${TGDB_SHELLCHECK_URL}" || true
  else
    wget -O "${TGDB_SHELLCHECK_TAR_PATH}" "${TGDB_SHELLCHECK_URL}" || true
  fi
fi
if [[ -f "${TGDB_SHELLCHECK_TAR_PATH}" ]]; then
  rm -rf "${TGDB_BASH_LS_STAGING_DIR}/shellcheck"
  mkdir -p "${TGDB_BASH_LS_STAGING_DIR}/shellcheck"
  if tar -xJf "${TGDB_SHELLCHECK_TAR_PATH}" -C "${TGDB_BASH_LS_STAGING_DIR}/shellcheck" --strip-components=1 2>/dev/null; then
    if [[ -x "${TGDB_BASH_LS_STAGING_DIR}/shellcheck/shellcheck" ]]; then
      install -m 0755 "${TGDB_BASH_LS_STAGING_DIR}/shellcheck/shellcheck" \
        "${TGDB_BASH_LS_PAYLOAD_DIR}/bin/shellcheck"
    fi
  fi
fi

# Normalize launcher.
cat > "${TGDB_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server" <<EOF
#!/bin/sh
ROOT="\$(CDPATH= cd -- "\$(dirname "\$0")/.." && pwd)"
exec "\$ROOT/bin/node" "\$ROOT/node_modules/bash-language-server/out/cli.js" "\$@"
EOF
chmod +x "${TGDB_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server"

rm -f "${TGDB_BASH_LS_TAR_PATH}"
tar -cf "${TGDB_BASH_LS_TAR_PATH}" -C "${TGDB_BASH_LS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_BASH_LS_TAR_PATH}" -o "${TGDB_BASH_LS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_BASH_LS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server" | awk '{print $1}')"

cat > "${TGDB_BASH_LS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_BASH_LS_VERSION "${TGDB_BASH_LS_VERSION}"
#define TGDB_BUNDLED_BASH_LS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_BASH_LS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_BASH_LS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_BASH_LS_ZST_PATH}")" "$(basename "${TGDB_BASH_LS_BLOB_OBJ}")"
)

printf 'bash ls bundle listo: %s (%s bytes)\n' \
  "${TGDB_BASH_LS_ZST_PATH}" "$(wc -c < "${TGDB_BASH_LS_ZST_PATH}")"
