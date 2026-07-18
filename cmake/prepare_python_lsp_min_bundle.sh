#!/usr/bin/env bash
# Empaqueta basedpyright (+ nodejs-wheel) para TUIDE_BUNDLE_PYTHON_LSP_MIN (opción A).
# Requiere python3 del host en runtime; no incluye CPython ni debugpy.
set -euo pipefail

die() {
  printf 'prepare_python_lsp_min_bundle: error: %s\n' "$*" >&2
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
require_var TUIDE_PYTHON_TOOLS_STAGING_DIR
require_var TUIDE_PYTHON_TOOLS_PAYLOAD_DIR
require_var TUIDE_PYTHON_TOOLS_TAR_PATH
require_var TUIDE_PYTHON_TOOLS_ZST_PATH
require_var TUIDE_PYTHON_TOOLS_MANIFEST_PATH
require_var TUIDE_PYTHON_TOOLS_MANIFEST_HPP
require_var TUIDE_PYTHON_TOOLS_BLOB_OBJ

TUIDE_PYTHON_TOOLS_VERSION="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_VERSION}")"
TUIDE_BASEDPYRIGHT_VERSION="$(strip_cmake_quotes "${TUIDE_BASEDPYRIGHT_VERSION}")"
TUIDE_PYTHON_TOOLS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_STAGING_DIR}")"
TUIDE_PYTHON_TOOLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}")"
TUIDE_PYTHON_TOOLS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_TAR_PATH}")"
TUIDE_PYTHON_TOOLS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_ZST_PATH}")"
TUIDE_PYTHON_TOOLS_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_MANIFEST_PATH}")"
TUIDE_PYTHON_TOOLS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_MANIFEST_HPP}")"
TUIDE_PYTHON_TOOLS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"

for tool in python3 zstd sha256sum objcopy tar; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada: ${tool}"
  fi
done

rm -rf "${TUIDE_PYTHON_TOOLS_STAGING_DIR}" "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_PYTHON_TOOLS_STAGING_DIR}" "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin" \
  "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python"

VENV_DIR="${TUIDE_PYTHON_TOOLS_STAGING_DIR}/venv"
python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/pip" install --upgrade pip
"${VENV_DIR}/bin/pip" install "basedpyright==${TUIDE_BASEDPYRIGHT_VERSION}"

SITE_PACKAGES="$(
  "${VENV_DIR}/bin/python" -c 'import site; print(site.getsitepackages()[0])'
)"
[[ -d "${SITE_PACKAGES}" ]] || die "no se encontró site-packages en el venv"

# Dereference symlinks so the simple tar extractor is happy.
cp -aL "${SITE_PACKAGES}/." "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python/"

cat > "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" <<'EOF'
#!/usr/bin/env python3
import os
import sys

_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_lib = os.path.join(_root, "lib", "python")
if _lib not in sys.path:
    sys.path.insert(0, _lib)

from basedpyright.langserver import main

if __name__ == "__main__":
    raise SystemExit(main())
EOF
chmod +x "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver"

# Ensure node binaries from nodejs-wheel are executable.
find "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python" -type f \( -name 'node' -o -name 'nodejs' \) \
  -exec chmod +x {} +

rm -f "${TUIDE_PYTHON_TOOLS_TAR_PATH}"
tar -cf "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -C "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_PYTHON_TOOLS_TAR_PATH}" -o "${TUIDE_PYTHON_TOOLS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_PYTHON_TOOLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" | awk '{print $1}')"

cat > "${TUIDE_PYTHON_TOOLS_MANIFEST_PATH}" <<EOF
{
  "version": "${TUIDE_PYTHON_TOOLS_VERSION}",
  "kind": "lsp_min",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "basedpyright_version": "${TUIDE_BASEDPYRIGHT_VERSION}"
}
EOF

cat > "${TUIDE_PYTHON_TOOLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_PYTHON_TOOLS_VERSION "${TUIDE_PYTHON_TOOLS_VERSION}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_BINARY_SHA256 "${bin_sha}"
#define TUIDE_BUNDLED_PYTHON_TOOLS_KIND_LSP_MIN 1
#define TUIDE_BUNDLED_PYTHON_TOOLS_KIND_FULL 0
EOF

blob_dir="$(dirname "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_PYTHON_TOOLS_ZST_PATH}")" "$(basename "${TUIDE_PYTHON_TOOLS_BLOB_OBJ}")"
)

printf 'python lsp_min bundle listo: %s (%s bytes)\n' \
  "${TUIDE_PYTHON_TOOLS_ZST_PATH}" "$(wc -c < "${TUIDE_PYTHON_TOOLS_ZST_PATH}")"
