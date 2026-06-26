#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

log() {
  printf '[compile] %s\n' "$*"
}

die() {
  printf '[compile] error: %s\n' "$*" >&2
  exit 1
}

check_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "no se encontró '$1' en PATH"
  fi
}

check_gdb_dap() {
  if ! gdb -i=dap -ex quit >/dev/null 2>&1; then
    die "GDB no soporta DAP. Necesitas GDB 14+ con Python (prueba: gdb -i=dap -ex quit)"
  fi
}

log "proyecto: ${ROOT}"
log "comprobando dependencias..."
check_command cmake
check_command g++
check_command gdb
check_gdb_dap

log "configurando CMake..."
cmake -S "${ROOT}" -B "${BUILD_DIR}"

log "compilando (${JOBS} hilos)..."
cmake --build "${BUILD_DIR}" -j "${JOBS}"

if [[ ! -x "${BUILD_DIR}/tgdb" ]]; then
  die "no se generó ${BUILD_DIR}/tgdb"
fi

if [[ ! -x "${BUILD_DIR}/hello" ]]; then
  die "no se generó ${BUILD_DIR}/hello"
fi

log "listo."
log "  tgdb:  ${BUILD_DIR}/tgdb"
log "  hello: ${BUILD_DIR}/hello"
log ""
log "lanza con: ${ROOT}/tools/launch.sh"
