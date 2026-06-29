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

warn_gdb_dap() {
  if ! command -v gdb >/dev/null 2>&1; then
    log "aviso: gdb no está instalado; la depuración quedará deshabilitada"
    return
  fi
  if ! gdb -i=dap -ex quit >/dev/null 2>&1; then
    log "aviso: GDB no soporta DAP (prueba: gdb -i=dap -ex quit); la depuración quedará deshabilitada"
  fi
}

log "proyecto: ${ROOT}"
log "comprobando dependencias..."
check_command cmake
check_command g++
warn_gdb_dap

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
