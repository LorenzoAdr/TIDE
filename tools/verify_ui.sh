#!/usr/bin/env bash
# Comprueba que tgdb pinta la interfaz (no pantalla negra).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
TGDB="${BUILD}/tgdb"
OUT="$(mktemp)"

if [[ ! -x "${TGDB}" ]]; then
  echo "verify_ui: compila primero (cmake --build build)" >&2
  exit 1
fi

cleanup() { rm -f "${OUT}"; }
trap cleanup EXIT

timeout 5 env TGDB_UI_SMOKE=1 "${TGDB}" --cwd "${ROOT}" > "${OUT}" 2>/dev/null || true

if ! grep -q 'tide' "${OUT}"; then
  echo "verify_ui: FALLO — no se encontró 'tide'" >&2
  echo "verify_ui: bytes: $(wc -c < "${OUT}")" >&2
  exit 1
fi

if ! grep -qE 'Editor|Outline|Explorador|Workspace|sin archivos' "${OUT}"; then
  echo "verify_ui: FALLO — no se encontró ningún panel esperado" >&2
  exit 1
fi

echo "verify_ui: OK — interfaz visible"
