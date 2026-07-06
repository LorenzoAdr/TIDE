#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"
CORE_DIR="${ROOT}/examples/cores"
TARGET="core_analyzer_demo"
CORE_NAME="${TARGET}.core"
BINARY="${BUILD}/${TARGET}"
CORE_PATH="${CORE_DIR}/${CORE_NAME}"

die() {
  printf 'generate_core_demo: error: %s\n' "$*" >&2
  exit 1
}

if [[ ! -d "${BUILD}" ]]; then
  die "no existe ${BUILD}; ejecuta primero: ./tools/compile.sh"
fi

cmake --build "${BUILD}" --target "${TARGET}"
mkdir -p "${CORE_DIR}"
rm -f "${CORE_PATH}" "${CORE_DIR}/core" "${CORE_DIR}/core."*

generate_with_gdb() {
  local gdb_bin="${1:-gdb}"
  (
    cd "${CORE_DIR}"
    "${gdb_bin}" -batch \
      -ex 'set pagination off' \
      -ex 'run' \
      -ex "gcore ${CORE_NAME}" \
      -ex quit \
      "${BINARY}"
  )
}

if command -v gdb >/dev/null 2>&1; then
  generate_with_gdb gdb
elif [[ -x "${BUILD}/tgdb" ]]; then
  die "gdb no está en PATH; instala gdb o usa: gdb -batch -ex run -ex 'gcore ${CORE_NAME}' ${BINARY}"
else
  ulimit -c unlimited
  (
    cd "${CORE_DIR}"
    "${BINARY}" || true
  )
  if [[ -f "${CORE_DIR}/core" ]]; then
    mv "${CORE_DIR}/core" "${CORE_PATH}"
  elif compgen -G "${CORE_DIR}/core."* >/dev/null; then
    latest="$(ls -t "${CORE_DIR}"/core.* | head -n1)"
    mv "${latest}" "${CORE_PATH}"
  else
    die "no se generó core (Apport puede interceptarlos; instala gdb y reintenta)"
  fi
fi

[[ -f "${CORE_PATH}" ]] || die "no se encontró ${CORE_PATH}"

printf '\nCore generado:\n  %s\n\nBinario:\n  %s\n\nProbar en tgdb:\n  %s/build/tgdb --core %s %s --core-analyzer\n\nNota: Core Analyzer necesita símbolos de debug de glibc\n  (Ubuntu/Debian: sudo apt install libc6-dbg)\n\nComandos Core Analyzer sugeridos:\n  obj (ConsoleSink*)0\n  obj (MetricStore*)0\n  ref 0x<dirección del MetricStore>\n  p *(ca_demo::MetricStore*)0x...\n' \
  "${CORE_PATH}" \
  "${BINARY}" \
  "${ROOT}" "${CORE_PATH}" "${BINARY}"
