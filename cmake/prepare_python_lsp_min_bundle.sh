#!/usr/bin/env bash
# Empaqueta basedpyright (+ nodejs-wheel) para TGDB_BUNDLE_PYTHON_LSP_MIN (opción A).
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

require_var TGDB_PYTHON_TOOLS_VERSION
require_var TGDB_BASEDPYRIGHT_VERSION
require_var TGDB_PYTHON_TOOLS_STAGING_DIR
require_var TGDB_PYTHON_TOOLS_PAYLOAD_DIR
require_var TGDB_PYTHON_TOOLS_TAR_PATH
require_var TGDB_PYTHON_TOOLS_ZST_PATH
require_var TGDB_PYTHON_TOOLS_MANIFEST_PATH
require_var TGDB_PYTHON_TOOLS_MANIFEST_HPP
require_var TGDB_PYTHON_TOOLS_BLOB_OBJ

TGDB_PYTHON_TOOLS_VERSION="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_VERSION}")"
TGDB_BASEDPYRIGHT_VERSION="$(strip_cmake_quotes "${TGDB_BASEDPYRIGHT_VERSION}")"
TGDB_PYTHON_TOOLS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_STAGING_DIR}")"
TGDB_PYTHON_TOOLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}")"
TGDB_PYTHON_TOOLS_TAR_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_TAR_PATH}")"
TGDB_PYTHON_TOOLS_ZST_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_ZST_PATH}")"
TGDB_PYTHON_TOOLS_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_MANIFEST_PATH}")"
TGDB_PYTHON_TOOLS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_MANIFEST_HPP}")"
TGDB_PYTHON_TOOLS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"

for tool in python3 zstd sha256sum objcopy tar; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada: ${tool}"
  fi
done

rm -rf "${TGDB_PYTHON_TOOLS_STAGING_DIR}" "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}"
mkdir -p "${TGDB_PYTHON_TOOLS_STAGING_DIR}" "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin" \
  "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python"

VENV_DIR="${TGDB_PYTHON_TOOLS_STAGING_DIR}/venv"
python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/pip" install --upgrade pip
"${VENV_DIR}/bin/pip" install "basedpyright==${TGDB_BASEDPYRIGHT_VERSION}"

SITE_PACKAGES="$(
  "${VENV_DIR}/bin/python" -c 'import site; print(site.getsitepackages()[0])'
)"
[[ -d "${SITE_PACKAGES}" ]] || die "no se encontró site-packages en el venv"

# Dereference symlinks so the simple tar extractor is happy.
cp -aL "${SITE_PACKAGES}/." "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python/"

cat > "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" <<'EOF'
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
chmod +x "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver"

# Ensure node binaries from nodejs-wheel are executable.
find "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/lib/python" -type f \( -name 'node' -o -name 'nodejs' \) \
  -exec chmod +x {} +

rm -f "${TGDB_PYTHON_TOOLS_TAR_PATH}"
tar -cf "${TGDB_PYTHON_TOOLS_TAR_PATH}" -C "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_PYTHON_TOOLS_TAR_PATH}" -o "${TGDB_PYTHON_TOOLS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_PYTHON_TOOLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_PYTHON_TOOLS_PAYLOAD_DIR}/bin/basedpyright-langserver" | awk '{print $1}')"

cat > "${TGDB_PYTHON_TOOLS_MANIFEST_PATH}" <<EOF
{
  "version": "${TGDB_PYTHON_TOOLS_VERSION}",
  "kind": "lsp_min",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "basedpyright_version": "${TGDB_BASEDPYRIGHT_VERSION}"
}
EOF

cat > "${TGDB_PYTHON_TOOLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_PYTHON_TOOLS_VERSION "${TGDB_PYTHON_TOOLS_VERSION}"
#define TGDB_BUNDLED_PYTHON_TOOLS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_PYTHON_TOOLS_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_PYTHON_TOOLS_KIND_LSP_MIN 1
#define TGDB_BUNDLED_PYTHON_TOOLS_KIND_FULL 0
EOF

blob_dir="$(dirname "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_PYTHON_TOOLS_ZST_PATH}")" "$(basename "${TGDB_PYTHON_TOOLS_BLOB_OBJ}")"
)

printf 'python lsp_min bundle listo: %s (%s bytes)\n' \
  "${TGDB_PYTHON_TOOLS_ZST_PATH}" "$(wc -c < "${TGDB_PYTHON_TOOLS_ZST_PATH}")"
