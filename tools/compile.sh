#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
CONFIG_FILE="${ROOT}/.bundle-config"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

BUNDLE_CLANGD=0
BUNDLE_CLANGD_FORCE=0
BUNDLE_GDB=0
BUNDLE_GDB_FORCE=0
INTERACTIVE=1
SKIP_WIZARD=0

log() {
  printf '[compile] %s\n' "$*"
}

die() {
  printf '[compile] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Uso: tools/compile.sh [opciones]

Sin opciones: TUI interactiva para elegir componentes embebidos.

Opciones:
  -y, --yes                  Usar .bundle-config sin TUI (o defaults si no existe)
  --non-interactive          Igual que --yes
  --bundle-clangd            Embeber clangd oficial
  --no-bundle-clangd         No embeber clangd
  --force-bundled-clangd     Forzar clangd embebido en runtime (requiere bundle)
  --no-force-bundled-clangd  Permitir fallback a clangd en PATH
  --bundle-gdb               Embeber gdb-static Full
  --no-bundle-gdb            No embeber gdb
  --force-bundled-gdb        Forzar gdb embebido en runtime (requiere bundle)
  --no-force-bundled-gdb     Permitir fallback a gdb en PATH
  -h, --help                 Mostrar esta ayuda

Variables de entorno:
  JOBS   Hilos para cmake --build (default: nproc)
EOF
}

check_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "no se encontró '$1' en PATH"
  fi
}

warn_gdb_dap() {
  if [[ "${BUNDLE_GDB}" == "1" ]]; then
    return
  fi
  if ! command -v gdb >/dev/null 2>&1; then
    log "aviso: gdb no está instalado; la depuración quedará deshabilitada (usa --bundle-gdb)"
    return
  fi
  if ! gdb -i=dap -ex quit >/dev/null 2>&1; then
    log "aviso: GDB no soporta DAP; la depuración quedará deshabilitada (usa --bundle-gdb)"
  fi
}

load_bundle_config() {
  BUNDLE_CLANGD=0
  BUNDLE_CLANGD_FORCE=0
  BUNDLE_GDB=0
  BUNDLE_GDB_FORCE=0
  if [[ ! -f "${CONFIG_FILE}" ]]; then
    return
  fi
  while IFS= read -r line || [[ -n "${line}" ]]; do
    case "${line}" in
      BUNDLE_CLANGD=1) BUNDLE_CLANGD=1 ;;
      BUNDLE_CLANGD=0) BUNDLE_CLANGD=0 ;;
      BUNDLE_CLANGD_FORCE=1) BUNDLE_CLANGD_FORCE=1 ;;
      BUNDLE_CLANGD_FORCE=0) BUNDLE_CLANGD_FORCE=0 ;;
      BUNDLE_GDB=1) BUNDLE_GDB=1 ;;
      BUNDLE_GDB=0) BUNDLE_GDB=0 ;;
      BUNDLE_GDB_FORCE=1) BUNDLE_GDB_FORCE=1 ;;
      BUNDLE_GDB_FORCE=0) BUNDLE_GDB_FORCE=0 ;;
    esac
  done < "${CONFIG_FILE}"
}

save_bundle_config() {
  cat > "${CONFIG_FILE}" <<EOF
BUNDLE_CLANGD=${BUNDLE_CLANGD}
BUNDLE_CLANGD_FORCE=${BUNDLE_CLANGD_FORCE}
BUNDLE_GDB=${BUNDLE_GDB}
BUNDLE_GDB_FORCE=${BUNDLE_GDB_FORCE}
EOF
}

run_wizard() {
  log "lanzando asistente de componentes embebidos..."
  if ! "${BUILD_DIR}/tgdb-bundle-wizard" "${CONFIG_FILE}"; then
    die "asistente cancelado"
  fi
  load_bundle_config
}

cmake_bundle_args() {
  local args=()
  if [[ "${BUNDLE_CLANGD}" == "1" ]]; then
    args+=(-DTGDB_BUNDLE_CLANGD=ON)
    if [[ "${BUNDLE_CLANGD_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_CLANGD=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_CLANGD=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_CLANGD=OFF -DTGDB_FORCE_BUNDLED_CLANGD=OFF)
  fi
  if [[ "${BUNDLE_GDB}" == "1" ]]; then
    args+=(-DTGDB_BUNDLE_GDB=ON)
    if [[ "${BUNDLE_GDB_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_GDB=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_GDB=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_GDB=OFF -DTGDB_FORCE_BUNDLED_GDB=OFF)
  fi
  printf '%s\n' "${args[@]}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes|--non-interactive)
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-clangd)
      BUNDLE_CLANGD=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-clangd)
      BUNDLE_CLANGD=0
      BUNDLE_CLANGD_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-clangd)
      BUNDLE_CLANGD_FORCE=1
      BUNDLE_CLANGD=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-clangd)
      BUNDLE_CLANGD_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-gdb)
      BUNDLE_GDB=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-gdb)
      BUNDLE_GDB=0
      BUNDLE_GDB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-gdb)
      BUNDLE_GDB_FORCE=1
      BUNDLE_GDB=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-gdb)
      BUNDLE_GDB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "opción desconocida: $1 (usa --help)"
      ;;
  esac
done

log "proyecto: ${ROOT}"
log "comprobando dependencias..."
check_command cmake
check_command g++

if [[ "${SKIP_WIZARD}" == "0" ]]; then
  log "configurando CMake (paso inicial)..."
  cmake -S "${ROOT}" -B "${BUILD_DIR}" \
    -DTGDB_BUNDLE_CLANGD=OFF -DTGDB_FORCE_BUNDLED_CLANGD=OFF \
    -DTGDB_BUNDLE_GDB=OFF -DTGDB_FORCE_BUNDLED_GDB=OFF
  log "compilando asistente de bundles..."
  cmake --build "${BUILD_DIR}" --target tgdb-bundle-wizard -j "${JOBS}"
  run_wizard
  save_bundle_config
else
  if [[ "${INTERACTIVE}" == "0" && "${BUNDLE_CLANGD}" == "0" && "${BUNDLE_CLANGD_FORCE}" == "0" && "${BUNDLE_GDB}" == "0" && "${BUNDLE_GDB_FORCE}" == "0" ]]; then
    load_bundle_config
  fi
fi

warn_gdb_dap

mapfile -t CMAKE_BUNDLE_ARGS < <(cmake_bundle_args)

log "configurando CMake..."
# shellcheck disable=SC2068
cmake -S "${ROOT}" -B "${BUILD_DIR}" ${CMAKE_BUNDLE_ARGS[@]}

log "compilando (${JOBS} hilos)..."
cmake --build "${BUILD_DIR}" -j "${JOBS}"

if [[ ! -x "${BUILD_DIR}/tgdb" ]]; then
  die "no se generó ${BUILD_DIR}/tgdb"
fi

if [[ ! -x "${BUILD_DIR}/hello" ]]; then
  die "no se generó ${BUILD_DIR}/hello"
fi

log "listo."
log "  tgdb:  ${BUILD_DIR}/tgdb ($(du -h "${BUILD_DIR}/tgdb" | awk '{print $1}'))"
log "  hello: ${BUILD_DIR}/hello"
if [[ "${BUNDLE_CLANGD}" == "1" ]]; then
  log "  clangd embebido: sí (force=${BUNDLE_CLANGD_FORCE})"
else
  log "  clangd embebido: no"
fi
if [[ "${BUNDLE_GDB}" == "1" ]]; then
  log "  gdb embebido: sí (force=${BUNDLE_GDB_FORCE})"
else
  log "  gdb embebido: no"
fi
log ""
log "lanza con: ${ROOT}/tools/launch.sh"
