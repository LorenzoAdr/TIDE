#!/usr/bin/env bash
# Empaqueta CPython portable + basedpyright + debugpy (opción B / TUIDE_BUNDLE_PYTHON_TOOLS).
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

require_var TUIDE_PYTHON_TOOLS_VERSION
require_var TUIDE_BASEDPYRIGHT_VERSION
require_var TUIDE_DEBUGPY_VERSION
require_var TUIDE_PYTHON_STANDALONE_URL
require_var TUIDE_PYTHON_STANDALONE_TAR_PATH
require_var TUIDE_PYTHON_TOOLS_STAGING_DIR
require_var TUIDE_PYTHON_TOOLS_PAYLOAD_DIR
require_var TUIDE_PYTHON_TOOLS_TAR_PATH
require_var TUIDE_PYTHON_TOOLS_ZST_PATH
require_var TUIDE_PYTHON_TOOLS_MANIFEST_PATH
require_var TUIDE_PYTHON_TOOLS_MANIFEST_HPP
require_var TUIDE_PYTHON_TOOLS_BLOB_OBJ

TUIDE_PYTHON_TOOLS_VERSION="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_VERSION}")"
TUIDE_BASEDPYRIGHT_VERSION="$(strip_cmake_quotes "${TUIDE_BASEDPYRIGHT_VERSION}")"
TUIDE_DEBUGPY_VERSION="$(strip_cmake_quotes "${TUIDE_DEBUGPY_VERSION}")"
TUIDE_PYTHON_STANDALONE_URL="$(strip_cmake_quotes "${TUIDE_PYTHON_STANDALONE_URL}")"
TUIDE_PYTHON_STANDALONE_TAR_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_STANDALONE_TAR_PATH}")"
TUIDE_PYTHON_TOOLS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_STAGING_DIR}")"
TUIDE_PYTHON_TOOLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}")"
TUIDE_PYTHON_TOOLS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_TAR_PATH}")"
TUIDE_PYTHON_TOOLS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_ZST_PATH}")"
TUIDE_PYTHON_TOOLS_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_MANIFEST_PATH}")"
TUIDE_PYTHON_TOOLS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_MANIFEST_HPP}")"
TUIDE_PYTHON_TOOLS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada: ${tool}"
  fi
done

if [[ ! -f "${TUIDE_PYTHON_STANDALONE_TAR_PATH}" ]]; then
  printf 'descargando %s...\n' "${TUIDE_PYTHON_STANDALONE_URL}"
  mkdir -p "$(dirname "${TUIDE_PYTHON_STANDALONE_TAR_PATH}")"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TUIDE_PYTHON_STANDALONE_TAR_PATH}" "${TUIDE_PYTHON_STANDALONE_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TUIDE_PYTHON_STANDALONE_TAR_PATH}" "${TUIDE_PYTHON_STANDALONE_URL}"
  else
    die "curl o wget requerido"
  fi
fi

rm -rf "${TUIDE_PYTHON_TOOLS_STAGING_DIR}" "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_PYTHON_TOOLS_STAGING_DIR}" "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}"

tar -xzf "${TUIDE_PYTHON_STANDALONE_TAR_PATH}" -C "${TUIDE_PYTHON_TOOLS_STAGING_DIR}"

# install_only layout: python/bin/python3
python_bin="$(find "${TUIDE_PYTHON_TOOLS_STAGING_DIR}" -type f -path '*/bin/python3' | head -n1)"
[[ -n "${python_bin}" && -x "${python_bin}" ]] || die "no se encontró bin/python3 en el standalone"
python_root="$(dirname "$(dirname "${python_bin}")")"

"${python_bin}" -m ensurepip --upgrade >/dev/null 2>&1 || true
"${python_bin}" -m pip install --upgrade pip
"${python_bin}" -m pip install \
  "basedpyright==${TUIDE_BASEDPYRIGHT_VERSION}" \
  "debugpy==${TUIDE_DEBUGPY_VERSION}"

# Flatten into payload root (bin/, lib/, ...) with dereferenced symlinks.
cp -aL "${python_root}/." "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/"

# Normalize launcher names.
if [[ ! -x "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python3" ]]; then
  if [[ -x "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python" ]]; then
    ln -sf python "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/python3"
  else
    die "payload sin bin/python3"
  fi
fi

[[ -x "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" ]] \
  || die "pip no instaló basedpyright-langserver"

# Replace any remaining symlinks under bin with real files when possible.
find "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin" -type l | while read -r link; do
  target="$(readlink -f "${link}" || true)"
  if [[ -n "${target}" && -f "${target}" ]]; then
    rm -f "${link}"
    cp -a "${target}" "${link}"
    chmod +x "${link}" || true
  fi
done

find "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}" -type f \( -name 'node' -o -name 'nodejs' -o -name 'python*' \) \
  -exec chmod +x {} + 2>/dev/null || true

rm -f "${TUIDE_PYTHON_TOOLS_TAR_PATH}"
# Prefer hard-dereference if supported.
if tar --help 2>&1 | grep -q -- '--hard-dereference'; then
  tar --hard-dereference -cf "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -C "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}" .
else
  tar -h -cf "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -C "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}" . 2>/dev/null \
    || tar -cf "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -C "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}" .
fi
zstd -f -19 -q "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -o "${TUIDE_PYTHON_TOOLS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_PYTHON_TOOLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" | awk '{print $1}')"

cat > "${TUIDE_PYTHON_TOOLS_MANIFEST_PATH}" <<EOF
{
  "version": "${TUIDE_PYTHON_TOOLS_VERSION}",
  "kind": "full",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "basedpyright_version": "${TUIDE_BASEDPYRIGHT_VERSION}",
  "debugpy_version": "${TUIDE_DEBUGPY_VERSION}"
}
EOF

cat > "${TUIDE_PYTHON_TOOLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_PYTHON_TOOLS_VERSION "${TUIDE_PYTHON_TOOLS_VERSION}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_BINARY_SHA256 "${bin_sha}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_KIND_LSP_MIN 0
#define TUIDE_BUNDLED_PYTHON_TOOLS_KIND_FULL 1
EOF

blob_dir="$(dirname "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_PYTHON_TOOLS_ZST_PATH}")" "$(basename "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"
)

printf 'python tools (full) bundle listo: %s (%s bytes)\n' \
  "${TUIDE_PYTHON_TOOLS_ZST_PATH}" "$(wc -c < "${TUIDE_PYTHON_TOOLS_ZST_PATH}")"
