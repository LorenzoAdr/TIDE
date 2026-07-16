#!/usr/bin/env bash
# Empaqueta CPython portable + basedpyright + debugpy (opción B / TGDB_BUNDLE_PYTHON_TOOLS).
set -euo pipefail

die() {
  printf 'prepare_python_tools_bundle: error: %s\n' "$*" >&2
  exit 1
}

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() {
  if [[ -z "${!1:-}" ]]; then
    die "variable de entorno requerida: $1"
  fi
}

require_var TGDB_PYTHON_TOOLS_VERSION
require_var TGDB_BASEDPYRIGHT_VERSION
require_var TGDB_DEBUGPY_VERSION
require_var TGDB_PYTHON_STANDALONE_URL
require_var TGDB_PYTHON_STANDALONE_TAR_PATH
require_var TGDB_PYTHON_TOOLS_STAGING_DIR
require_var TGDB_PYTHON_TOOLS_PAYLOAD_DIR
require_var TGDB_PYTHON_TOOLS_TAR_PATH
require_var TGDB_PYTHON_TOOLS_ZST_PATH
require_var TGDB_PYTHON_TOOLS_MANIFEST_PATH
require_var TGDB_PYTHON_TOOLS_MANIFEST_HPP
require_var TGDB_PYTHON_TOOLS_BLOB_OBJ

TGDB_PYTHON_TOOLS_VERSION="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_VERSION}")"
TGDB_BASEDPYRIGHT_VERSION="$(strip_cmake_quotes "${TGDB_BASEDPYRIGHT_VERSION}")"
TGDB_DEBUGPY_VERSION="$(strip_cmake_quotes "${TGDB_DEBUGPY_VERSION}")"
TGDB_PYTHON_STANDALONE_URL="$(strip_cmake_quotes "${TGDB_PYTHON_STANDALONE_URL}")"
TGDB_PYTHON_STANDALONE_TAR_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_STANDALONE_TAR_PATH}")"
TGDB_PYTHON_TOOLS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_STAGING_DIR}")"
TGDB_PYTHON_TOOLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}")"
TGDB_PYTHON_TOOLS_TAR_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_TAR_PATH}")"
TGDB_PYTHON_TOOLS_ZST_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_ZST_PATH}")"
TGDB_PYTHON_TOOLS_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_MANIFEST_PATH}")"
TGDB_PYTHON_TOOLS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_MANIFEST_HPP}")"
TGDB_PYTHON_TOOLS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada: ${tool}"
  fi
done

if [[ ! -f "${TGDB_PYTHON_STANDALONE_TAR_PATH}" ]]; then
  printf 'descargando %s...\n' "${TGDB_PYTHON_STANDALONE_URL}"
  mkdir -p "$(dirname "${TGDB_PYTHON_STANDALONE_TAR_PATH}")"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TGDB_PYTHON_STANDALONE_TAR_PATH}" "${TGDB_PYTHON_STANDALONE_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TGDB_PYTHON_STANDALONE_TAR_PATH}" "${TGDB_PYTHON_STANDALONE_URL}"
  else
    die "curl o wget requerido"
  fi
fi

rm -rf "${TGDB_PYTHON_TOOLS_STAGING_DIR}" "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}"
mkdir -p "${TGDB_PYTHON_TOOLS_STAGING_DIR}" "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}"

tar -xzf "${TGDB_PYTHON_STANDALONE_TAR_PATH}" -C "${TGDB_PYTHON_TOOLS_STAGING_DIR}"

# install_only layout: python/bin/python3
python_bin="$(find "${TGDB_PYTHON_TOOLS_STAGING_DIR}" -type f -path '*/bin/python3' | head -n1)"
[[ -n "${python_bin}" && -x "${python_bin}" ]] || die "no se encontró bin/python3 en el standalone"
python_root="$(dirname "$(dirname "${python_bin}")")"

"${python_bin}" -m ensurepip --upgrade >/dev/null 2>&1 || true
"${python_bin}" -m pip install --upgrade pip
"${python_bin}" -m pip install \
  "basedpyright==${TGDB_BASEDPYRIGHT_VERSION}" \
  "debugpy==${TGDB_DEBUGPY_VERSION}"

# Flatten into payload root (bin/, lib/, ...) with dereferenced symlinks.
cp -aL "${python_root}/." "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/"

# Normalize launcher names.
if [[ ! -x "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python3" ]]; then
  if [[ -x "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python" ]]; then
    ln -sf python "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python3"
  else
    die "payload sin bin/python3"
  fi
fi

[[ -x "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" ]] \
  || die "pip no instaló basedpyright-langserver"

# Replace any remaining symlinks under bin with real files when possible.
find "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin" -type l | while read -r link; do
  target="$(readlink -f "${link}" || true)"
  if [[ -n "${target}" && -f "${target}" ]]; then
    rm -f "${link}"
    cp -a "${target}" "${link}"
    chmod +x "${link}" || true
  fi
done

find "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}" -type f \( -name 'node' -o -name 'nodejs' -o -name 'python*' \) \
  -exec chmod +x {} + 2>/dev/null || true

rm -f "${TGDB_PYTHON_TOOLS_TAR_PATH}"
# Prefer hard-dereference if supported.
if tar --help 2>&1 | grep -q -- '--hard-dereference'; then
  tar --hard-dereference -cf "${TGDB_PYTHON_TOOLS_TAR_PATH}" -C "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}" .
else
  tar -h -cf "${TGDB_PYTHON_TOOLS_TAR_PATH}" -C "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}" . 2>/dev/null \
    || tar -cf "${TGDB_PYTHON_TOOLS_TAR_PATH}" -C "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}" .
fi
zstd -f -19 -q "${TGDB_PYTHON_TOOLS_TAR_PATH}" -o "${TGDB_PYTHON_TOOLS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_PYTHON_TOOLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" | awk '{print $1}')"

cat > "${TGDB_PYTHON_TOOLS_MANIFEST_PATH}" <<EOF
{
  "version": "${TGDB_PYTHON_TOOLS_VERSION}",
  "kind": "full",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "basedpyright_version": "${TGDB_BASEDPYRIGHT_VERSION}",
  "debugpy_version": "${TGDB_DEBUGPY_VERSION}"
}
EOF

cat > "${TGDB_PYTHON_TOOLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_PYTHON_TOOLS_VERSION "${TGDB_PYTHON_TOOLS_VERSION}"
#define TGDB_BUNDLED_PYTHON_TOOLS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_PYTHON_TOOLS_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_PYTHON_TOOLS_KIND_LSP_MIN 0
#define TGDB_BUNDLED_PYTHON_TOOLS_KIND_FULL 1
EOF

blob_dir="$(dirname "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_PYTHON_TOOLS_ZST_PATH}")" "$(basename "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"
)

printf 'python tools (full) bundle listo: %s (%s bytes)\n' \
  "${TGDB_PYTHON_TOOLS_ZST_PATH}" "$(wc -c < "${TGDB_PYTHON_TOOLS_ZST_PATH}")"
