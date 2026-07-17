#!/usr/bin/env bash
# Empaqueta fortls (site-packages + wrapper python3). Requiere python3/pip en bundle time;
# en runtime el host debe tener python3 en PATH.
set -euo pipefail

die() { printf 'prepare_fortls_bundle: error: %s\n' "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_FORTLS_VERSION
require_var TGDB_FORTLS_STAGING_DIR
require_var TGDB_FORTLS_PAYLOAD_DIR
require_var TGDB_FORTLS_TAR_PATH
require_var TGDB_FORTLS_ZST_PATH
require_var TGDB_FORTLS_MANIFEST_HPP
require_var TGDB_FORTLS_BLOB_OBJ

TGDB_FORTLS_VERSION="$(strip_cmake_quotes "${TGDB_FORTLS_VERSION}")"
TGDB_FORTLS_STAGING_DIR="$(strip_cmake_quotes "${TGDB_FORTLS_STAGING_DIR}")"
TGDB_FORTLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_FORTLS_PAYLOAD_DIR}")"
TGDB_FORTLS_TAR_PATH="$(strip_cmake_quotes "${TGDB_FORTLS_TAR_PATH}")"
TGDB_FORTLS_ZST_PATH="$(strip_cmake_quotes "${TGDB_FORTLS_ZST_PATH}")"
TGDB_FORTLS_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_FORTLS_MANIFEST_HPP}")"
TGDB_FORTLS_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_FORTLS_BLOB_OBJ}")"

for tool in python3 zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool} (fortls requiere python3 en tiempo de bundle)"
done

rm -rf "${TGDB_FORTLS_STAGING_DIR}" "${TGDB_FORTLS_PAYLOAD_DIR}"
mkdir -p "${TGDB_FORTLS_STAGING_DIR}" \
  "${TGDB_FORTLS_PAYLOAD_DIR}/bin" \
  "${TGDB_FORTLS_PAYLOAD_DIR}/lib/python"

VENV_DIR="${TGDB_FORTLS_STAGING_DIR}/venv"
python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/pip" install --upgrade pip
"${VENV_DIR}/bin/pip" install "fortls==${TGDB_FORTLS_VERSION}"

SITE_PACKAGES="$(
  "${VENV_DIR}/bin/python" -c 'import site; print(site.getsitepackages()[0])'
)"
[[ -d "${SITE_PACKAGES}" ]] || die "no se encontró site-packages en el venv"
cp -aL "${SITE_PACKAGES}/." "${TGDB_FORTLS_PAYLOAD_DIR}/lib/python/"

cat > "${TGDB_FORTLS_PAYLOAD_DIR}/bin/fortls" <<'EOF'
#!/usr/bin/env python3
import os
import sys

_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_lib = os.path.join(_root, "lib", "python")
if _lib not in sys.path:
    sys.path.insert(0, _lib)

from fortls import main

if __name__ == "__main__":
    raise SystemExit(main())
EOF
chmod +x "${TGDB_FORTLS_PAYLOAD_DIR}/bin/fortls"

rm -f "${TGDB_FORTLS_TAR_PATH}"
tar -cf "${TGDB_FORTLS_TAR_PATH}" -C "${TGDB_FORTLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_FORTLS_TAR_PATH}" -o "${TGDB_FORTLS_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_FORTLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_FORTLS_PAYLOAD_DIR}/bin/fortls" | awk '{print $1}')"

cat > "${TGDB_FORTLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_FORTLS_VERSION "${TGDB_FORTLS_VERSION}"
#define TGDB_BUNDLED_FORTLS_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_FORTLS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TGDB_FORTLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_FORTLS_ZST_PATH}")" "$(basename "${TGDB_FORTLS_BLOB_OBJ}")"
)

printf 'fortls bundle listo: %s (%s bytes)\n' \
  "${TGDB_FORTLS_ZST_PATH}" "$(wc -c < "${TGDB_FORTLS_ZST_PATH}")"
