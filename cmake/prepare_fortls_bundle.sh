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

require_var TUIDE_FORTLS_VERSION
require_var TUIDE_FORTLS_STAGING_DIR
require_var TUIDE_FORTLS_PAYLOAD_DIR
require_var TUIDE_FORTLS_TAR_PATH
require_var TUIDE_FORTLS_ZST_PATH
require_var TUIDE_FORTLS_MANIFEST_HPP
require_var TUIDE_FORTLS_BLOB_OBJ

TUIDE_FORTLS_VERSION="$(strip_cmake_quotes "${TUIDE_FORTLS_VERSION}")"
TUIDE_FORTLS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_FORTLS_STAGING_DIR}")"
TUIDE_FORTLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_FORTLS_PAYLOAD_DIR}")"
TUIDE_FORTLS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_FORTLS_TAR_PATH}")"
TUIDE_FORTLS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_FORTLS_ZST_PATH}")"
TUIDE_FORTLS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_FORTLS_MANIFEST_HPP}")"
TUIDE_FORTLS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_FORTLS_BLOB_OBJ}")"

for tool in python3 zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool} (fortls requiere python3 en tiempo de bundle)"
done

rm -rf "${TUIDE_FORTLS_STAGING_DIR}" "${TUIDE_FORTLS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_FORTLS_STAGING_DIR}" \
  "${TUIDE_FORTLS_PAYLOAD_DIR}/bin" \
  "${TUIDE_FORTLS_PAYLOAD_DIR}/lib/python"

VENV_DIR="${TUIDE_FORTLS_STAGING_DIR}/venv"
python3 -m venv "${VENV_DIR}"
"${VENV_DIR}/bin/pip" install --upgrade pip
"${VENV_DIR}/bin/pip" install "fortls==${TUIDE_FORTLS_VERSION}"

SITE_PACKAGES="$(
  "${VENV_DIR}/bin/python" -c 'import site; print(site.getsitepackages()[0])'
)"
[[ -d "${SITE_PACKAGES}" ]] || die "no se encontró site-packages en el venv"
cp -aL "${SITE_PACKAGES}/." "${TUIDE_FORTLS_PAYLOAD_DIR}/lib/python/"

cat > "${TUIDE_FORTLS_PAYLOAD_DIR}/bin/fortls" <<'EOF'
#!/usr/bin/env python3
import os
import sys

_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_lib = os.path.join(_root, "lib", "python")
if _lib not in sys.path:
    sys.path.insert(0, _lib)

# fortls only publishes diagnostics on didOpen/didSave; patch didChange so edits
# refresh squiggles without requiring a save (matches editor expectations).
from fortls.langserver import LangServer

_orig_on_change = LangServer.serve_onChange


def _on_change_with_diagnostics(self, request):
    _orig_on_change(self, request)
    if getattr(self, "disable_diagnostics", False):
        return
    try:
        uri = request.get("params", {}).get("textDocument", {}).get("uri")
    except Exception:
        return
    if uri:
        self.send_diagnostics(uri)


LangServer.serve_onChange = _on_change_with_diagnostics

from fortls import main

if __name__ == "__main__":
    raise SystemExit(main())
EOF
chmod +x "${TUIDE_FORTLS_PAYLOAD_DIR}/bin/fortls"

rm -f "${TUIDE_FORTLS_TAR_PATH}"
tar -cf "${TUIDE_FORTLS_TAR_PATH}" -C "${TUIDE_FORTLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_FORTLS_TAR_PATH}" -o "${TUIDE_FORTLS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_FORTLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_FORTLS_PAYLOAD_DIR}/bin/fortls" | awk '{print $1}')"

cat > "${TUIDE_FORTLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_FORTLS_VERSION "${TUIDE_FORTLS_VERSION}"
#define TUIDE_BUNDLED_FORTLS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_FORTLS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_FORTLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_FORTLS_ZST_PATH}")" "$(basename "${TUIDE_FORTLS_BLOB_OBJ}")"
)

printf 'fortls bundle listo: %s (%s bytes)\n' \
  "${TUIDE_FORTLS_ZST_PATH}" "$(wc -c < "${TUIDE_FORTLS_ZST_PATH}")"
