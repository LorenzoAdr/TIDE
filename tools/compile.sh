#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
CONFIG_FILE="${ROOT}/.bundle-config"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

BUNDLE_CLANGD=0
BUNDLE_CLANGD_FORCE=0
GDB_BUNDLE_KIND=none
BUNDLE_GDB=0
BUNDLE_GDB_FORCE=0
PYTHON_BUNDLE_KIND=none
BUNDLE_PYTHON_FORCE=0
BUNDLE_BASH_LS=0
BUNDLE_BASH_LS_FORCE=0
BUNDLE_TEXLAB=0
BUNDLE_TEXLAB_FORCE=0
BUNDLE_BASH_DAP=0
BUNDLE_BASH_DAP_FORCE=0
BUILD_GDB_CA=0
STATIC_LIBSTDCXX=0
INTERACTIVE=1
SKIP_WIZARD=0
CLI_OVERRIDES_BUNDLE=0

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
  --bundle-gdb               Embeber gdb (tipo según .bundle-config o static)
  --bundle-gdb-static        Embeber gdb-static (musl, sin Core Analyzer)
  --bundle-gdb-ca            Embeber gdb + Core Analyzer
  --no-bundle-gdb            No embeber gdb
  --build-gdb-ca             Compilar gdb+CA si falta tarball (solo --bundle-gdb-ca)
  --force-bundled-gdb        Forzar gdb embebido en runtime (requiere bundle)
  --no-force-bundled-gdb     Permitir fallback a gdb en PATH
  --bundle-python-lsp-min    Embeber basedpyright (opción A; Python del host)
  --bundle-python-tools      Embeber CPython + basedpyright + debugpy (opción B)
  --no-bundle-python         No embeber herramientas Python
  --force-bundled-python     Forzar herramientas Python embebidas en runtime
  --no-force-bundled-python  Permitir fallback a PATH / venv del host
  --bundle-bash-ls           Embeber bash-language-server + Node
  --no-bundle-bash-ls        No embeber bash-language-server
  --force-bundled-bash-ls    Forzar bash-language-server embebido en runtime (requiere bundle)
  --no-force-bundled-bash-ls Permitir fallback a bash-language-server en PATH
  --bundle-texlab            Embeber TexLab + chktex (LSP LaTeX)
  --no-bundle-texlab         No embeber TexLab
  --force-bundled-texlab     Forzar TexLab embebido en runtime (requiere bundle)
  --no-force-bundled-texlab  Permitir fallback a texlab en PATH
  --bundle-bash-dap          Embeber adaptador Bash DAP + bashdb
  --no-bundle-bash-dap       No embeber Bash DAP
  --force-bundled-bash-dap   Forzar Bash DAP embebido en runtime (requiere bundle)
  --no-force-bundled-bash-dap Permitir fallback a adaptador Bash DAP en PATH
  --static-libstdc++         Enlazar libstdc++/libgcc estáticamente (menos deps en runtime)
  -h, --help                 Mostrar esta ayuda

Variables de entorno:
  JOBS              Hilos para cmake --build (default: nproc)
  CMAKE_BUILD_TYPE  Tipo de build CMake (ej. Release)
EOF
}

check_command() {
  if ! command -v "$1" >/dev/null 2>&1; then
    die "no se encontró '$1' en PATH"
  fi
}

sync_gdb_bundle_flags() {
  if [[ "${GDB_BUNDLE_KIND}" != "none" ]]; then
    BUNDLE_GDB=1
  else
    BUNDLE_GDB=0
    BUNDLE_GDB_FORCE=0
  fi
}

sync_python_bundle_flags() {
  case "${PYTHON_BUNDLE_KIND}" in
    lsp_min|full)
      ;;
    *)
      PYTHON_BUNDLE_KIND=none
      BUNDLE_PYTHON_FORCE=0
      ;;
  esac
  if [[ "${PYTHON_BUNDLE_KIND}" == "none" ]]; then
    BUNDLE_PYTHON_FORCE=0
  fi
}

sync_bash_tex_bundle_flags() {
  if [[ "${BUNDLE_BASH_LS}" != "1" ]]; then
    BUNDLE_BASH_LS_FORCE=0
  fi
  if [[ "${BUNDLE_TEXLAB}" != "1" ]]; then
    BUNDLE_TEXLAB_FORCE=0
  fi
  if [[ "${BUNDLE_BASH_DAP}" != "1" ]]; then
    BUNDLE_BASH_DAP_FORCE=0
  fi
}

warn_gdb_dap() {
  sync_gdb_bundle_flags
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

gdb_static_tarball_path() {
  printf '%s/third_party/bundled/cache/v16.3-static-gdb-static-full-x86_64.tar.gz' "${ROOT}"
}

gdb_ca_tarball_path() {
  printf '%s/third_party/bundled/cache/gdb-ca-16.3-ca.tar.gz' "${ROOT}"
}

check_gdb_ca_build_deps() {
  local missing=()
  if ! echo '#include <gmp.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libgmp-dev")
  fi
  if ! echo '#include <mpfr.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libmpfr-dev")
  fi
  if ! echo '#include <mpc.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libmpc-dev")
  fi
  if [[ ${#missing[@]} -gt 0 ]]; then
    die "faltan paquetes para compilar gdb+Core Analyzer: ${missing[*]}. Instala con: sudo apt install ${missing[*]}"
  fi
}

ensure_gdb_ca_tarball() {
  sync_gdb_bundle_flags
  if [[ "${GDB_BUNDLE_KIND}" != "core_analyzer" ]]; then
    return 0
  fi

  local tarball
  tarball="$(gdb_ca_tarball_path)"
  if [[ -f "${tarball}" ]]; then
    return 0
  fi

  mkdir -p "$(dirname "${tarball}")"

  if command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    log "tarball gdb+Core Analyzer no encontrado: ${tarball}"
    log "generando con docker/Dockerfile.gdb-ca (puede tardar 30+ min)..."
    if docker build -f "${ROOT}/docker/Dockerfile.gdb-ca" -t tgdb-gdb-ca "${ROOT}"; then
      docker run --rm tgdb-gdb-ca > "${tarball}"
      log "tarball generado: ${tarball}"
      return 0
    fi
    log "aviso: build docker falló; intentando compilación nativa..."
  elif command -v docker >/dev/null 2>&1; then
    log "aviso: docker sin permisos; omitiendo build en contenedor"
  fi

  if [[ "${BUILD_GDB_CA}" != "1" ]]; then
    log "tarball gdb+Core Analyzer no encontrado: ${tarball}"
    log "activando compilación nativa de gdb+Core Analyzer (puede tardar 30+ min)..."
    BUILD_GDB_CA=1
  fi
  check_gdb_ca_build_deps
}

load_bundle_config() {
  BUNDLE_CLANGD=0
  BUNDLE_CLANGD_FORCE=0
  GDB_BUNDLE_KIND=none
  BUNDLE_GDB=0
  BUNDLE_GDB_FORCE=0
  PYTHON_BUNDLE_KIND=none
  BUNDLE_PYTHON_FORCE=0
  BUNDLE_BASH_LS=0
  BUNDLE_BASH_LS_FORCE=0
  BUNDLE_TEXLAB=0
  BUNDLE_TEXLAB_FORCE=0
  BUNDLE_BASH_DAP=0
  BUNDLE_BASH_DAP_FORCE=0
  if [[ ! -f "${CONFIG_FILE}" ]]; then
    return
  fi
  local legacy_bundle_gdb=0
  while IFS= read -r line || [[ -n "${line}" ]]; do
    case "${line}" in
      BUNDLE_CLANGD=1) BUNDLE_CLANGD=1 ;;
      BUNDLE_CLANGD=0) BUNDLE_CLANGD=0 ;;
      BUNDLE_CLANGD_FORCE=1) BUNDLE_CLANGD_FORCE=1 ;;
      BUNDLE_CLANGD_FORCE=0) BUNDLE_CLANGD_FORCE=0 ;;
      GDB_BUNDLE_KIND=static) GDB_BUNDLE_KIND=static ;;
      GDB_BUNDLE_KIND=core_analyzer) GDB_BUNDLE_KIND=core_analyzer ;;
      GDB_BUNDLE_KIND=none) GDB_BUNDLE_KIND=none ;;
      BUNDLE_GDB=1) legacy_bundle_gdb=1 ;;
      BUNDLE_GDB=0) legacy_bundle_gdb=0 ;;
      BUNDLE_GDB_FORCE=1) BUNDLE_GDB_FORCE=1 ;;
      BUNDLE_GDB_FORCE=0) BUNDLE_GDB_FORCE=0 ;;
      PYTHON_BUNDLE_KIND=lsp_min) PYTHON_BUNDLE_KIND=lsp_min ;;
      PYTHON_BUNDLE_KIND=full) PYTHON_BUNDLE_KIND=full ;;
      PYTHON_BUNDLE_KIND=none) PYTHON_BUNDLE_KIND=none ;;
      BUNDLE_PYTHON_FORCE=1) BUNDLE_PYTHON_FORCE=1 ;;
      BUNDLE_PYTHON_FORCE=0) BUNDLE_PYTHON_FORCE=0 ;;
      BUNDLE_BASH_LS=1) BUNDLE_BASH_LS=1 ;;
      BUNDLE_BASH_LS=0) BUNDLE_BASH_LS=0 ;;
      BUNDLE_BASH_LS_FORCE=1) BUNDLE_BASH_LS_FORCE=1 ;;
      BUNDLE_BASH_LS_FORCE=0) BUNDLE_BASH_LS_FORCE=0 ;;
      BUNDLE_TEXLAB=1) BUNDLE_TEXLAB=1 ;;
      BUNDLE_TEXLAB=0) BUNDLE_TEXLAB=0 ;;
      BUNDLE_TEXLAB_FORCE=1) BUNDLE_TEXLAB_FORCE=1 ;;
      BUNDLE_TEXLAB_FORCE=0) BUNDLE_TEXLAB_FORCE=0 ;;
      BUNDLE_BASH_DAP=1) BUNDLE_BASH_DAP=1 ;;
      BUNDLE_BASH_DAP=0) BUNDLE_BASH_DAP=0 ;;
      BUNDLE_BASH_DAP_FORCE=1) BUNDLE_BASH_DAP_FORCE=1 ;;
      BUNDLE_BASH_DAP_FORCE=0) BUNDLE_BASH_DAP_FORCE=0 ;;
    esac
  done < "${CONFIG_FILE}"
  if [[ "${GDB_BUNDLE_KIND}" == "none" && "${legacy_bundle_gdb}" == "1" ]]; then
    GDB_BUNDLE_KIND=static
  fi
  sync_gdb_bundle_flags
  sync_python_bundle_flags
  sync_bash_tex_bundle_flags
  # Migrar configs antiguas sin GDB_BUNDLE_KIND explícito.
  if [[ "${legacy_bundle_gdb}" == "1" ]] && ! grep -q '^GDB_BUNDLE_KIND=' "${CONFIG_FILE}" 2>/dev/null; then
    save_bundle_config
    log "migrado .bundle-config: GDB_BUNDLE_KIND=static (gdb embebido legacy)"
  fi
}

save_bundle_config() {
  sync_gdb_bundle_flags
  sync_python_bundle_flags
  sync_bash_tex_bundle_flags
  cat > "${CONFIG_FILE}" <<EOF
BUNDLE_CLANGD=${BUNDLE_CLANGD}
BUNDLE_CLANGD_FORCE=${BUNDLE_CLANGD_FORCE}
GDB_BUNDLE_KIND=${GDB_BUNDLE_KIND}
BUNDLE_GDB=${BUNDLE_GDB}
BUNDLE_GDB_FORCE=${BUNDLE_GDB_FORCE}
PYTHON_BUNDLE_KIND=${PYTHON_BUNDLE_KIND}
BUNDLE_PYTHON_FORCE=${BUNDLE_PYTHON_FORCE}
BUNDLE_BASH_LS=${BUNDLE_BASH_LS}
BUNDLE_BASH_LS_FORCE=${BUNDLE_BASH_LS_FORCE}
BUNDLE_TEXLAB=${BUNDLE_TEXLAB}
BUNDLE_TEXLAB_FORCE=${BUNDLE_TEXLAB_FORCE}
BUNDLE_BASH_DAP=${BUNDLE_BASH_DAP}
BUNDLE_BASH_DAP_FORCE=${BUNDLE_BASH_DAP_FORCE}
EOF
}

run_wizard() {
  log "lanzando asistente de componentes embebidos..."
  if ! "${BUILD_DIR}/tgdb-bundle-wizard" "${CONFIG_FILE}"; then
    die "asistente cancelado"
  fi
  load_bundle_config
}

cmake_extra_args() {
  local args=()
  if [[ -n "${CMAKE_BUILD_TYPE:-}" ]]; then
    args+=(-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
  fi
  if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
    args+=(-DTGDB_STATIC_LIBSTDCXX=ON)
  fi
  printf '%s\n' "${args[@]}"
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
    args+=(-DTGDB_BUNDLE_GDB=ON -DTGDB_GDB_BUNDLE_KIND="${GDB_BUNDLE_KIND}")
    if [[ "${GDB_BUNDLE_KIND}" == "core_analyzer" ]]; then
      if [[ "${BUILD_GDB_CA}" == "1" ]] || [[ ! -f "$(gdb_ca_tarball_path)" ]]; then
        args+=(-DTGDB_BUILD_GDB_CA=ON)
      else
        args+=(-DTGDB_BUILD_GDB_CA=OFF)
      fi
    else
      args+=(-DTGDB_BUILD_GDB_CA=OFF)
    fi
    if [[ "${BUNDLE_GDB_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_GDB=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_GDB=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_GDB=OFF -DTGDB_FORCE_BUNDLED_GDB=OFF)
  fi
  case "${PYTHON_BUNDLE_KIND}" in
    lsp_min)
      args+=(-DTGDB_BUNDLE_PYTHON_LSP_MIN=ON -DTGDB_BUNDLE_PYTHON_TOOLS=OFF)
      if [[ "${BUNDLE_PYTHON_FORCE}" == "1" ]]; then
        args+=(-DTGDB_FORCE_BUNDLED_PYTHON_TOOLS=ON)
      else
        args+=(-DTGDB_FORCE_BUNDLED_PYTHON_TOOLS=OFF)
      fi
      ;;
    full)
      args+=(-DTGDB_BUNDLE_PYTHON_TOOLS=ON -DTGDB_BUNDLE_PYTHON_LSP_MIN=OFF)
      if [[ "${BUNDLE_PYTHON_FORCE}" == "1" ]]; then
        args+=(-DTGDB_FORCE_BUNDLED_PYTHON_TOOLS=ON)
      else
        args+=(-DTGDB_FORCE_BUNDLED_PYTHON_TOOLS=OFF)
      fi
      ;;
    *)
      args+=(-DTGDB_BUNDLE_PYTHON_LSP_MIN=OFF -DTGDB_BUNDLE_PYTHON_TOOLS=OFF \
             -DTGDB_FORCE_BUNDLED_PYTHON_TOOLS=OFF)
      ;;
  esac
  if [[ "${BUNDLE_BASH_LS}" == "1" ]]; then
    args+=(-DTGDB_BUNDLE_BASH_LS=ON)
    if [[ "${BUNDLE_BASH_LS_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_BASH_LS=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_BASH_LS=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_BASH_LS=OFF -DTGDB_FORCE_BUNDLED_BASH_LS=OFF)
  fi
  if [[ "${BUNDLE_TEXLAB}" == "1" ]]; then
    args+=(-DTGDB_BUNDLE_TEXLAB=ON)
    # Pin versions without '~' — CMake/make quote KEY=VALUE oddly and curl rejects the URL.
    args+=(-DTGDB_CHKTEX_VERSION=1.7.10-1 -DTGDB_PCRE2_VERSION=10.47-2)
    if [[ "${BUNDLE_TEXLAB_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_TEXLAB=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_TEXLAB=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_TEXLAB=OFF -DTGDB_FORCE_BUNDLED_TEXLAB=OFF)
  fi
  if [[ "${BUNDLE_BASH_DAP}" == "1" ]]; then
    args+=(-DTGDB_BUNDLE_BASH_DAP=ON)
    if [[ "${BUNDLE_BASH_DAP_FORCE}" == "1" ]]; then
      args+=(-DTGDB_FORCE_BUNDLED_BASH_DAP=ON)
    else
      args+=(-DTGDB_FORCE_BUNDLED_BASH_DAP=OFF)
    fi
  else
    args+=(-DTGDB_BUNDLE_BASH_DAP=OFF -DTGDB_FORCE_BUNDLED_BASH_DAP=OFF)
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
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_CLANGD=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-clangd)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_CLANGD=0
      BUNDLE_CLANGD_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-clangd)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_CLANGD_FORCE=1
      BUNDLE_CLANGD=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-clangd)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_CLANGD_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-gdb)
      CLI_OVERRIDES_BUNDLE=1
      GDB_BUNDLE_KIND=static
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-gdb-static)
      CLI_OVERRIDES_BUNDLE=1
      GDB_BUNDLE_KIND=static
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-gdb-ca)
      CLI_OVERRIDES_BUNDLE=1
      GDB_BUNDLE_KIND=core_analyzer
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --build-gdb-ca)
      CLI_OVERRIDES_BUNDLE=1
      BUILD_GDB_CA=1
      GDB_BUNDLE_KIND=core_analyzer
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-gdb)
      CLI_OVERRIDES_BUNDLE=1
      GDB_BUNDLE_KIND=none
      BUNDLE_GDB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-gdb)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_GDB_FORCE=1
      if [[ "${GDB_BUNDLE_KIND}" == "none" ]]; then
        GDB_BUNDLE_KIND=static
      fi
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-gdb)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_GDB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-python-lsp-min)
      CLI_OVERRIDES_BUNDLE=1
      PYTHON_BUNDLE_KIND=lsp_min
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-python-tools)
      CLI_OVERRIDES_BUNDLE=1
      PYTHON_BUNDLE_KIND=full
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-python)
      CLI_OVERRIDES_BUNDLE=1
      PYTHON_BUNDLE_KIND=none
      BUNDLE_PYTHON_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-python)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_PYTHON_FORCE=1
      if [[ "${PYTHON_BUNDLE_KIND}" == "none" ]]; then
        PYTHON_BUNDLE_KIND=lsp_min
      fi
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-python)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_PYTHON_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_LS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_LS=0
      BUNDLE_BASH_LS_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_LS_FORCE=1
      BUNDLE_BASH_LS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_LS_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-texlab)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TEXLAB=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-texlab)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TEXLAB=0
      BUNDLE_TEXLAB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-texlab)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TEXLAB_FORCE=1
      BUNDLE_TEXLAB=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-texlab)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TEXLAB_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_DAP=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_DAP=0
      BUNDLE_BASH_DAP_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_DAP_FORCE=1
      BUNDLE_BASH_DAP=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_DAP_FORCE=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --static-libstdc++)
      STATIC_LIBSTDCXX=1
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
  # Configure with the current .bundle-config (or defaults), not with all
  # bundles forced OFF. An OFF→ON dance rewrites tgdb's flags.make and makes
  # Make rebuild every translation unit even when the wizard selection is unchanged.
  load_bundle_config
  sync_gdb_bundle_flags
  sync_python_bundle_flags
  sync_bash_tex_bundle_flags
  mapfile -t CMAKE_BUNDLE_ARGS < <(cmake_bundle_args)
  mapfile -t CMAKE_EXTRA_ARGS < <(cmake_extra_args)
  log "configurando CMake (asistente)..."
  # shellcheck disable=SC2068
  cmake -S "${ROOT}" -B "${BUILD_DIR}" ${CMAKE_BUNDLE_ARGS[@]} ${CMAKE_EXTRA_ARGS[@]}
  log "compilando asistente de bundles..."
  cmake --build "${BUILD_DIR}" --target tgdb-bundle-wizard -j "${JOBS}"
  run_wizard
  save_bundle_config
else
  if [[ "${SKIP_WIZARD}" == "1" && "${CLI_OVERRIDES_BUNDLE}" == "0" ]]; then
    load_bundle_config
  fi
fi

sync_gdb_bundle_flags
sync_python_bundle_flags
sync_bash_tex_bundle_flags

warn_gdb_dap
ensure_gdb_ca_tarball

mapfile -t CMAKE_BUNDLE_ARGS < <(cmake_bundle_args)
mapfile -t CMAKE_EXTRA_ARGS < <(cmake_extra_args)

log "configurando CMake..."
if [[ "${BUNDLE_GDB}" == "1" ]]; then
  log "gdb embebido: ${GDB_BUNDLE_KIND}"
  if [[ "${GDB_BUNDLE_KIND}" == "static" ]]; then
    log "  tarball: $(gdb_static_tarball_path)"
    if [[ -f "$(gdb_static_tarball_path)" ]]; then
      log "  (caché encontrada; no compila gdb)"
    else
      log "  (descargará gdb-static de GitHub)"
    fi
  fi
fi
# shellcheck disable=SC2068
cmake -S "${ROOT}" -B "${BUILD_DIR}" ${CMAKE_BUNDLE_ARGS[@]} ${CMAKE_EXTRA_ARGS[@]}

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
  log "  gdb embebido: ${GDB_BUNDLE_KIND} (force=${BUNDLE_GDB_FORCE})"
else
  log "  gdb embebido: no"
fi
case "${PYTHON_BUNDLE_KIND}" in
  lsp_min)
    log "  python embebido: A / lsp_min (force=${BUNDLE_PYTHON_FORCE})"
    ;;
  full)
    log "  python embebido: B / full (force=${BUNDLE_PYTHON_FORCE})"
    ;;
  *)
    log "  python embebido: no"
    ;;
esac
if [[ "${BUNDLE_BASH_LS}" == "1" ]]; then
  log "  bash-language-server embebido: sí (force=${BUNDLE_BASH_LS_FORCE})"
else
  log "  bash-language-server embebido: no"
fi
if [[ "${BUNDLE_TEXLAB}" == "1" ]]; then
  log "  texlab+chktex embebido: sí (force=${BUNDLE_TEXLAB_FORCE})"
else
  log "  texlab embebido: no"
fi
if [[ "${BUNDLE_BASH_DAP}" == "1" ]]; then
  log "  bash DAP embebido: sí (force=${BUNDLE_BASH_DAP_FORCE})"
else
  log "  bash DAP embebido: no"
fi
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  log "  libstdc++ estático: sí"
fi
log ""
log "lanza con: ${ROOT}/tools/launch.sh"
