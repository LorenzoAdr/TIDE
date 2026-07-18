#!/usr/bin/env bash
# Empaqueta bash-language-server + Node portable (TUIDE_BUNDLE_BASH_LS).
set -euo pipefail

die() { printf 'prepare_bash_ls_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"; value="${value#\"}"; value="${value%\"}"; printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TUIDE_BASH_LS_VERSION
require_var TUIDE_BASH_LS_NPM_VERSION
require_var TUIDE_NODE_URL
require_var TUIDE_NODE_TAR_PATH
require_var TUIDE_BASH_LS_STAGING_DIR
require_var TUIDE_BASH_LS_PAYLOAD_DIR
require_var TUIDE_BASH_LS_TAR_PATH
require_var TUIDE_BASH_LS_ZST_PATH
require_var TUIDE_BASH_LS_MANIFEST_HPP
require_var TUIDE_BASH_LS_BLOB_OBJ

TUIDE_BASH_LS_VERSION="$(strip_cmake_quotes "${TUIDE_BASH_LS_VERSION}")"
TUIDE_BASH_LS_NPM_VERSION="$(strip_cmake_quotes "${TUIDE_BASH_LS_NPM_VERSION}")"
TUIDE_NODE_URL="$(strip_cmake_quotes "${TUIDE_NODE_URL}")"
TUIDE_NODE_TAR_PATH="$(strip_cmake_quotes "${TUIDE_NODE_TAR_PATH}")"
TUIDE_BASH_LS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_BASH_LS_STAGING_DIR}")"
TUIDE_BASH_LS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_BASH_LS_PAYLOAD_DIR}")"
TUIDE_BASH_LS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_BASH_LS_TAR_PATH}")"
TUIDE_BASH_LS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_BASH_LS_ZST_PATH}")"
TUIDE_BASH_LS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_BASH_LS_MANIFEST_HPP}")"
TUIDE_BASH_LS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_BASH_LS_BLOB_OBJ}")"

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

rm -rf "${TUIDE_BASH_LS_STAGING_DIR}" "${TUIDE_BASH_LS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_BASH_LS_STAGING_DIR}/node" "${TUIDE_BASH_LS_PAYLOAD_DIR}"
tar -xJf "${TUIDE_NODE_TAR_PATH}" -C "${TUIDE_BASH_LS_STAGING_DIR}/node" --strip-components=1
cp -a "${TUIDE_BASH_LS_STAGING_DIR}/node/." "${TUIDE_BASH_LS_PAYLOAD_DIR}/"
NODE_BIN="${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/node"
NPM_BIN="${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/npm"
chmod +x "${NODE_BIN}" "${NPM_BIN}" || true

# Install bash-language-server into the portable node prefix.
"${NPM_BIN}" install --prefix "${TUIDE_BASH_LS_PAYLOAD_DIR}" "bash-language-server@${TUIDE_BASH_LS_NPM_VERSION}"

# Optional ShellCheck static binary for diagnostics (SCxxxx).
TUIDE_BUNDLED_CACHE_DIR="$(dirname "${TUIDE_NODE_TAR_PATH}")"
TUIDE_SHELLCHECK_VERSION="${TUIDE_SHELLCHECK_VERSION:-0.10.0}"
TUIDE_SHELLCHECK_TAR_NAME="shellcheck-v${TUIDE_SHELLCHECK_VERSION}.linux.x86_64.tar.xz"
TUIDE_SHELLCHECK_URL="https://github.com/koalaman/shellcheck/releases/download/v${TUIDE_SHELLCHECK_VERSION}/${TUIDE_SHELLCHECK_TAR_NAME}"
TUIDE_SHELLCHECK_TAR_PATH="${TUIDE_BUNDLED_CACHE_DIR}/${TUIDE_SHELLCHECK_TAR_NAME}"
if [[ ! -f "${TUIDE_SHELLCHECK_TAR_PATH}" ]]; then
  mkdir -p "$(dirname "${TUIDE_SHELLCHECK_TAR_PATH}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${TUIDE_SHELLCHECK_TAR_PATH}" "${TUIDE_SHELLCHECK_URL}" || true
  else
    wget -O "${TUIDE_SHELLCHECK_TAR_PATH}" "${TUIDE_SHELLCHECK_URL}" || true
  fi
fi
if [[ -f "${TUIDE_SHELLCHECK_TAR_PATH}" ]]; then
  rm -rf "${TUIDE_BASH_LS_STAGING_DIR}/shellcheck"
  mkdir -p "${TUIDE_BASH_LS_STAGING_DIR}/shellcheck"
  if tar -xJf "${TUIDE_SHELLCHECK_TAR_PATH}" -C "${TUIDE_BASH_LS_STAGING_DIR}/shellcheck" --strip-components=1 2>/dev/null; then
    if [[ -x "${TUIDE_BASH_LS_STAGING_DIR}/shellcheck/shellcheck" ]]; then
      install -m 0755 "${TUIDE_BASH_LS_STAGING_DIR}/shellcheck/shellcheck" \
        "${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/shellcheck"
    fi
  fi
fi

# Normalize launcher.
cat > "${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server" <<EOF
#!/bin/sh
ROOT="\$(CDPATH= cd -- "\$(dirname "\$0")/.." && pwd)"
exec "\$ROOT/bin/node" "\$ROOT/node_modules/bash-language-server/out/cli.js" "\$@"
EOF
chmod +x "${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server"

rm -f "${TUIDE_BASH_LS_TAR_PATH}"
tar -cf "${TUIDE_BASH_LS_TAR_PATH}" -C "${TUIDE_BASH_LS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_BASH_LS_TAR_PATH}" -o "${TUIDE_BASH_LS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_BASH_LS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_BASH_LS_PAYLOAD_DIR}/bin/bash-language-server" | awk '{print $1}')"

cat > "${TUIDE_BASH_LS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_BASH_LS_VERSION "${TUIDE_BASH_LS_VERSION}"
#define TUIDE_BUNDLED_BASH_LS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_BASH_LS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_BASH_LS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_BASH_LS_ZST_PATH}")" "$(basename "${TUIDE_BASH_LS_BLOB_OBJ}")"
)

printf 'bash ls bundle listo: %s (%s bytes)\n' \
  "${TUIDE_BASH_LS_ZST_PATH}" "$(wc -c < "${TUIDE_BASH_LS_ZST_PATH}")"
