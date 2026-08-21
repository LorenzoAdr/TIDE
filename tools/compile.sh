#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
# Árbol CMake aislado solo para el asistente TUI: no toca flags ni objetos de build/.
WIZARD_BUILD_DIR="${ROOT}/build-wizard"
WIZARD_SRC="${ROOT}/tools/bundle_wizard/main.cpp"
CONFIG_FILE="${ROOT}/.bundle-config"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
WIZARD_BIN=""

BUNDLE_CLANGD=0
GDB_BUNDLE_KIND=none
BUNDLE_GDB=0
PYTHON_BUNDLE_KIND=none
BUNDLE_BASH_LS=0
BUNDLE_TEXLAB=0
BUNDLE_BASH_DAP=0
BUNDLE_RUST_ANALYZER=0
BUNDLE_GOPLS=0
BUNDLE_ZLS=0
BUNDLE_FORTLS=0
BUNDLE_LUA_LS=0
BUNDLE_TSSERVER=0
BUNDLE_NEOCMAKELSP=0
BUNDLE_MAKE_LS=0
BUNDLE_YAML_LS=0
BUNDLE_LEMMINX=0
FORCE_BUNDLED=0
UI_LOCALE=en
EDITOR_MODE=normal
BUILD_BACKEND=host
BUILD_GDB_CA=0
STATIC_LIBSTDCXX=0
STATIC_LIBSTDCXX_FROM_CLI=0
BUILD_BACKEND_FROM_CLI=0
# La TUI de bundles queda desactivada por defecto (toolpacks vía GitHub Releases).
# Usar --wizard / --interactive para abrirla; el código del asistente se conserva.
INTERACTIVE=0
SKIP_WIZARD=1
CLI_OVERRIDES_BUNDLE=0
# Si compile.sh se ejecuta dentro de la imagen portable, no re-lanzar Docker.
IN_PORTABLE_CONTAINER="${TUIDE_IN_PORTABLE_CONTAINER:-0}"
# Targets opcionales para cmake --build (vacío = todo). Ej: --target tuide
BUILD_TARGETS=()
REQUIRE_HELLO=1

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

Sin opciones: compilación directa con .bundle-config (o defaults si no existe).
La TUI de componentes embebidos está desactivada por defecto; ábrela con --wizard.

Opciones:
  -y, --yes                  Usar .bundle-config sin TUI (o defaults si no existe)
  --non-interactive          Igual que --yes
  --wizard, --interactive    Abrir la TUI de selección de componentes embebidos
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
  --force-bundled            Forzar todos los componentes seleccionados (sin fallback PATH)
  --no-force-bundled         Permitir fallback al sistema para componentes embebidos
  --ui-locale=es|en          Idioma por defecto de la aplicación (español / inglés)
  --editor-mode=normal|helix Modo de editor por defecto (Normal / Helix)
  --bundle-rust-analyzer     Embeber rust-analyzer
  --no-bundle-rust-analyzer  No embeber rust-analyzer
  --bundle-gopls             Embeber gopls
  --no-bundle-gopls          No embeber gopls
  --bundle-zls               Embeber zls
  --no-bundle-zls            No embeber zls
  --bundle-fortls            Embeber fortls
  --no-bundle-fortls         No embeber fortls
  --bundle-lua-ls            Embeber lua-language-server
  --no-bundle-lua-ls         No embeber lua-language-server
  --bundle-tsserver          Embeber typescript-language-server
  --no-bundle-tsserver       No embeber typescript-language-server
  --bundle-neocmakelsp       Embeber neocmakelsp
  --no-bundle-neocmakelsp    No embeber neocmakelsp
  --bundle-make-ls           Embeber make-ls
  --no-bundle-make-ls        No embeber make-ls
  --bundle-yaml-ls           Embeber yaml-language-server
  --no-bundle-yaml-ls        No embeber yaml-language-server
  --bundle-lemminx           Embeber LemMinX
  --no-bundle-lemminx        No embeber LemMinX
  --build-backend=host|docker_focal|docker_bionic
                             Dónde compilar (host o Docker con glibc antigua)
  --target NAME              Solo compilar el target CMake (repetible; default: todos)
  --static-libstdc++         Enlazar libstdc++/libgcc estáticamente (menos deps en runtime)
  -h, --help                 Mostrar esta ayuda

Variables de entorno:
  JOBS              Hilos para cmake --build (default: nproc)
  CMAKE_BUILD_TYPE  Tipo de build CMake (ej. Release)
  TUIDE_IN_PORTABLE_CONTAINER  1 = ya estamos en la imagen portable (no re-lanzar Docker)
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
  fi
}

sync_python_bundle_flags() {
  case "${PYTHON_BUNDLE_KIND}" in
    lsp_min|full)
      ;;
    *)
      PYTHON_BUNDLE_KIND=none
      ;;
  esac
}

force_bundled_cmake() {
  if [[ "${FORCE_BUNDLED}" == "1" ]]; then
    printf 'ON'
  else
    printf 'OFF'
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
    if docker build -f "${ROOT}/docker/Dockerfile.gdb-ca" -t tuide-gdb-ca "${ROOT}"; then
      docker run --rm tuide-gdb-ca > "${tarball}"
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
  GDB_BUNDLE_KIND=none
  BUNDLE_GDB=0
  PYTHON_BUNDLE_KIND=none
  BUNDLE_BASH_LS=0
  BUNDLE_TEXLAB=0
  BUNDLE_BASH_DAP=0
  BUNDLE_RUST_ANALYZER=0
  BUNDLE_GOPLS=0
  BUNDLE_ZLS=0
  BUNDLE_FORTLS=0
  BUNDLE_LUA_LS=0
  BUNDLE_TSSERVER=0
  BUNDLE_NEOCMAKELSP=0
  BUNDLE_MAKE_LS=0
  BUNDLE_YAML_LS=0
  BUNDLE_LEMMINX=0
  FORCE_BUNDLED=0
  UI_LOCALE=en
  EDITOR_MODE=normal
  if [[ "${BUILD_BACKEND_FROM_CLI}" != "1" ]]; then
    BUILD_BACKEND=host
  fi
  if [[ "${STATIC_LIBSTDCXX_FROM_CLI}" != "1" ]]; then
    STATIC_LIBSTDCXX=0
  fi
  if [[ ! -f "${CONFIG_FILE}" ]]; then
    return
  fi
  local legacy_bundle_gdb=0
  local legacy_force=0
  while IFS= read -r line || [[ -n "${line}" ]]; do
    case "${line}" in
      BUNDLE_CLANGD=1) BUNDLE_CLANGD=1 ;;
      BUNDLE_CLANGD=0) BUNDLE_CLANGD=0 ;;
      BUNDLE_CLANGD_FORCE=1) legacy_force=1 ;;
      GDB_BUNDLE_KIND=static) GDB_BUNDLE_KIND=static ;;
      GDB_BUNDLE_KIND=core_analyzer) GDB_BUNDLE_KIND=core_analyzer ;;
      GDB_BUNDLE_KIND=none) GDB_BUNDLE_KIND=none ;;
      BUNDLE_GDB=1) legacy_bundle_gdb=1 ;;
      BUNDLE_GDB=0) legacy_bundle_gdb=0 ;;
      BUNDLE_GDB_FORCE=1) legacy_force=1 ;;
      PYTHON_BUNDLE_KIND=lsp_min) PYTHON_BUNDLE_KIND=lsp_min ;;
      PYTHON_BUNDLE_KIND=full) PYTHON_BUNDLE_KIND=full ;;
      PYTHON_BUNDLE_KIND=none) PYTHON_BUNDLE_KIND=none ;;
      BUNDLE_PYTHON_FORCE=1) legacy_force=1 ;;
      BUNDLE_BASH_LS=1) BUNDLE_BASH_LS=1 ;;
      BUNDLE_BASH_LS=0) BUNDLE_BASH_LS=0 ;;
      BUNDLE_BASH_LS_FORCE=1) legacy_force=1 ;;
      BUNDLE_TEXLAB=1) BUNDLE_TEXLAB=1 ;;
      BUNDLE_TEXLAB=0) BUNDLE_TEXLAB=0 ;;
      BUNDLE_TEXLAB_FORCE=1) legacy_force=1 ;;
      BUNDLE_BASH_DAP=1) BUNDLE_BASH_DAP=1 ;;
      BUNDLE_BASH_DAP=0) BUNDLE_BASH_DAP=0 ;;
      BUNDLE_BASH_DAP_FORCE=1) legacy_force=1 ;;
      BUNDLE_RUST_ANALYZER=1) BUNDLE_RUST_ANALYZER=1 ;;
      BUNDLE_RUST_ANALYZER=0) BUNDLE_RUST_ANALYZER=0 ;;
      BUNDLE_GOPLS=1) BUNDLE_GOPLS=1 ;;
      BUNDLE_GOPLS=0) BUNDLE_GOPLS=0 ;;
      BUNDLE_ZLS=1) BUNDLE_ZLS=1 ;;
      BUNDLE_ZLS=0) BUNDLE_ZLS=0 ;;
      BUNDLE_FORTLS=1) BUNDLE_FORTLS=1 ;;
      BUNDLE_FORTLS=0) BUNDLE_FORTLS=0 ;;
      BUNDLE_LUA_LS=1) BUNDLE_LUA_LS=1 ;;
      BUNDLE_LUA_LS=0) BUNDLE_LUA_LS=0 ;;
      BUNDLE_TSSERVER=1) BUNDLE_TSSERVER=1 ;;
      BUNDLE_TSSERVER=0) BUNDLE_TSSERVER=0 ;;
      BUNDLE_NEOCMAKELSP=1) BUNDLE_NEOCMAKELSP=1 ;;
      BUNDLE_NEOCMAKELSP=0) BUNDLE_NEOCMAKELSP=0 ;;
      BUNDLE_MAKE_LS=1) BUNDLE_MAKE_LS=1 ;;
      BUNDLE_MAKE_LS=0) BUNDLE_MAKE_LS=0 ;;
      BUNDLE_YAML_LS=1) BUNDLE_YAML_LS=1 ;;
      BUNDLE_YAML_LS=0) BUNDLE_YAML_LS=0 ;;
      BUNDLE_LEMMINX=1) BUNDLE_LEMMINX=1 ;;
      BUNDLE_LEMMINX=0) BUNDLE_LEMMINX=0 ;;
      FORCE_BUNDLED=1) FORCE_BUNDLED=1 ;;
      FORCE_BUNDLED=0) FORCE_BUNDLED=0 ;;
      UI_LOCALE=es) UI_LOCALE=es ;;
      UI_LOCALE=en) UI_LOCALE=en ;;
      EDITOR_MODE=normal) EDITOR_MODE=normal ;;
      EDITOR_MODE=helix) EDITOR_MODE=helix ;;
      BUILD_BACKEND=host)
        if [[ "${BUILD_BACKEND_FROM_CLI}" != "1" ]]; then
          BUILD_BACKEND=host
        fi
        ;;
      BUILD_BACKEND=docker_focal|BUILD_BACKEND=docker|BUILD_BACKEND=portable)
        if [[ "${BUILD_BACKEND_FROM_CLI}" != "1" ]]; then
          BUILD_BACKEND=docker_focal
        fi
        ;;
      BUILD_BACKEND=docker_bionic|BUILD_BACKEND=bionic)
        if [[ "${BUILD_BACKEND_FROM_CLI}" != "1" ]]; then
          BUILD_BACKEND=docker_bionic
        fi
        ;;
      STATIC_LIBSTDCXX=1)
        if [[ "${STATIC_LIBSTDCXX_FROM_CLI}" != "1" ]]; then
          STATIC_LIBSTDCXX=1
        fi
        ;;
      STATIC_LIBSTDCXX=0)
        if [[ "${STATIC_LIBSTDCXX_FROM_CLI}" != "1" ]]; then
          STATIC_LIBSTDCXX=0
        fi
        ;;
    esac
  done < "${CONFIG_FILE}"
  if [[ "${GDB_BUNDLE_KIND}" == "none" && "${legacy_bundle_gdb}" == "1" ]]; then
    GDB_BUNDLE_KIND=static
  fi
  if [[ "${legacy_force}" == "1" ]]; then
    FORCE_BUNDLED=1
  fi
  case "${UI_LOCALE}" in
    es|en) ;;
    *) UI_LOCALE=en ;;
  esac
  case "${EDITOR_MODE}" in
    normal|helix) ;;
    *) EDITOR_MODE=normal ;;
  esac
  case "${BUILD_BACKEND}" in
    host|docker_focal|docker_bionic) ;;
    *) BUILD_BACKEND=host ;;
  esac
  # No forzar BUILD_BACKEND=host aquí cuando TUIDE_IN_PORTABLE_CONTAINER=1:
  # el docker run ya pasa --build-backend=host, y un save de migración
  # no debe reescribir la preferencia Docker del árbol montado.
  sync_gdb_bundle_flags
  sync_python_bundle_flags
  # Migrar configs antiguas sin GDB_BUNDLE_KIND explícito.
  if [[ "${legacy_bundle_gdb}" == "1" ]] && ! grep -q '^GDB_BUNDLE_KIND=' "${CONFIG_FILE}" 2>/dev/null; then
    save_bundle_config
    log "migrado .bundle-config: GDB_BUNDLE_KIND=static (gdb embebido legacy)"
  fi
}

save_bundle_config() {
  sync_gdb_bundle_flags
  sync_python_bundle_flags
  cat > "${CONFIG_FILE}" <<EOF
BUNDLE_CLANGD=${BUNDLE_CLANGD}
GDB_BUNDLE_KIND=${GDB_BUNDLE_KIND}
BUNDLE_GDB=${BUNDLE_GDB}
PYTHON_BUNDLE_KIND=${PYTHON_BUNDLE_KIND}
BUNDLE_BASH_LS=${BUNDLE_BASH_LS}
BUNDLE_TEXLAB=${BUNDLE_TEXLAB}
BUNDLE_BASH_DAP=${BUNDLE_BASH_DAP}
BUNDLE_RUST_ANALYZER=${BUNDLE_RUST_ANALYZER}
BUNDLE_GOPLS=${BUNDLE_GOPLS}
BUNDLE_ZLS=${BUNDLE_ZLS}
BUNDLE_FORTLS=${BUNDLE_FORTLS}
BUNDLE_LUA_LS=${BUNDLE_LUA_LS}
BUNDLE_TSSERVER=${BUNDLE_TSSERVER}
BUNDLE_NEOCMAKELSP=${BUNDLE_NEOCMAKELSP}
BUNDLE_MAKE_LS=${BUNDLE_MAKE_LS}
BUNDLE_YAML_LS=${BUNDLE_YAML_LS}
BUNDLE_LEMMINX=${BUNDLE_LEMMINX}
FORCE_BUNDLED=${FORCE_BUNDLED}
UI_LOCALE=${UI_LOCALE}
EDITOR_MODE=${EDITOR_MODE}
BUILD_BACKEND=${BUILD_BACKEND}
STATIC_LIBSTDCXX=${STATIC_LIBSTDCXX}
EOF
}

wizard_bin_fresh() {
  local candidate="$1"
  [[ -x "${candidate}" && "${candidate}" -nt "${WIZARD_SRC}" ]]
}

find_fresh_wizard_bin() {
  local candidate
  for candidate in "${BUILD_DIR}/tuide-bundle-wizard" "${WIZARD_BUILD_DIR}/tuide-bundle-wizard"; do
    if wizard_bin_fresh "${candidate}"; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

# Compila el asistente en un proyecto mínimo (solo FTXUI) para no tocar build/
# ni arrastrar tree-sitter/cppdap/bundles antes de la selección.
build_wizard_isolated() {
  if [[ -f "${WIZARD_BUILD_DIR}/CMakeCache.txt" ]]; then
    local cached_home
    cached_home="$(grep -E '^CMAKE_HOME_DIRECTORY:' "${WIZARD_BUILD_DIR}/CMakeCache.txt" \
      | head -1 | cut -d= -f2- || true)"
    if [[ -n "${cached_home}" && "${cached_home}" != "${ROOT}/tools/bundle_wizard" ]]; then
      log "limpiando ${WIZARD_BUILD_DIR} (CMake de otro proyecto)..."
      rm -rf "${WIZARD_BUILD_DIR}"
    fi
  fi
  local cmake_args=()
  # Reutilizar el sources de ftxui del build principal si ya existe (evita reclonar).
  if [[ -d "${BUILD_DIR}/_deps/ftxui-src" ]]; then
    cmake_args+=(-DFETCHCONTENT_SOURCE_DIR_FTXUI="${BUILD_DIR}/_deps/ftxui-src")
  fi
  log "preparando asistente de bundles (proyecto mínimo, solo ftxui)..."
  # shellcheck disable=SC2068
  cmake -S "${ROOT}/tools/bundle_wizard" -B "${WIZARD_BUILD_DIR}" ${cmake_args[@]}
  cmake --build "${WIZARD_BUILD_DIR}" --target tuide-bundle-wizard -j "${JOBS}"
  WIZARD_BIN="${WIZARD_BUILD_DIR}/tuide-bundle-wizard"
  [[ -x "${WIZARD_BIN}" ]] || die "no se generó ${WIZARD_BIN}"
}

ensure_bundle_wizard() {
  local found
  if found="$(find_fresh_wizard_bin)"; then
    WIZARD_BIN="${found}"
    log "asistente listo: ${WIZARD_BIN}"
    return 0
  fi
  # Si build/ ya está configurado, recompilar solo el wizard ahí (reutiliza ftxui)
  # sin reconfigurar flags de bundles.
  if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    log "actualizando asistente de bundles en ${BUILD_DIR}..."
    cmake --build "${BUILD_DIR}" --target tuide-bundle-wizard -j "${JOBS}"
    WIZARD_BIN="${BUILD_DIR}/tuide-bundle-wizard"
    if [[ -x "${WIZARD_BIN}" ]]; then
      return 0
    fi
    log "aviso: no se pudo actualizar el asistente en build/; probando proyecto mínimo"
  fi
  build_wizard_isolated
}

run_wizard() {
  ensure_bundle_wizard
  log "lanzando asistente de componentes embebidos..."
  if ! "${WIZARD_BIN}" "${CONFIG_FILE}"; then
    die "asistente cancelado"
  fi
  load_bundle_config
}

# Si el wizard / config pide Docker portable, delegar a build-portable.sh
# (compila con -y respetando .bundle-config) y salir.
maybe_delegate_portable_build() {
  if [[ "${IN_PORTABLE_CONTAINER}" == "1" ]]; then
    return 0
  fi
  case "${BUILD_BACKEND}" in
    docker_focal|docker_bionic) ;;
    *) return 0 ;;
  esac

  local args=()
  if [[ "${BUILD_BACKEND}" == "docker_bionic" ]]; then
    args+=(--bionic)
  fi
  if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
    args+=(--static-libstdc++)
  fi
  args+=(--jobs "${JOBS}")

  log "backend Docker (${BUILD_BACKEND}): delegando a tools/build-portable.sh"
  log "  (usa .bundle-config del wizard; no fuerza packs extra)"
  exec "${ROOT}/tools/build-portable.sh" "${args[@]}"
}

cmake_extra_args() {
  local args=()
  if [[ -n "${CMAKE_BUILD_TYPE:-}" ]]; then
    args+=(-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
  fi
  if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
    args+=(-DTUIDE_STATIC_LIBSTDCXX=ON)
  fi
  printf '%s\n' "${args[@]}"
}

cmake_bundle_args() {
  local args=()
  local force
  force="$(force_bundled_cmake)"
  if [[ "${BUNDLE_CLANGD}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_CLANGD=ON -DTUIDE_FORCE_BUNDLED_CLANGD="${force}")
  else
    args+=(-DTUIDE_BUNDLE_CLANGD=OFF -DTUIDE_FORCE_BUNDLED_CLANGD=OFF)
  fi
  if [[ "${BUNDLE_GDB}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_GDB=ON -DTUIDE_GDB_BUNDLE_KIND="${GDB_BUNDLE_KIND}")
    if [[ "${GDB_BUNDLE_KIND}" == "core_analyzer" ]]; then
      if [[ "${BUILD_GDB_CA}" == "1" ]] || [[ ! -f "$(gdb_ca_tarball_path)" ]]; then
        args+=(-DTUIDE_BUILD_GDB_CA=ON)
      else
        args+=(-DTUIDE_BUILD_GDB_CA=OFF)
      fi
    else
      args+=(-DTUIDE_BUILD_GDB_CA=OFF)
    fi
    args+=(-DTUIDE_FORCE_BUNDLED_GDB="${force}")
  else
    args+=(-DTUIDE_BUNDLE_GDB=OFF -DTUIDE_FORCE_BUNDLED_GDB=OFF)
  fi
  case "${PYTHON_BUNDLE_KIND}" in
    lsp_min)
      args+=(-DTUIDE_BUNDLE_PYTHON_LSP_MIN=ON -DTUIDE_BUNDLE_PYTHON_TOOLS=OFF)
      args+=(-DTUIDE_FORCE_BUNDLED_PYTHON_TOOLS="${force}")
      ;;
    full)
      args+=(-DTUIDE_BUNDLE_PYTHON_TOOLS=ON -DTUIDE_BUNDLE_PYTHON_LSP_MIN=OFF)
      args+=(-DTUIDE_FORCE_BUNDLED_PYTHON_TOOLS="${force}")
      ;;
    *)
      args+=(-DTUIDE_BUNDLE_PYTHON_LSP_MIN=OFF -DTUIDE_BUNDLE_PYTHON_TOOLS=OFF \
             -DTUIDE_FORCE_BUNDLED_PYTHON_TOOLS=OFF)
      ;;
  esac
  if [[ "${BUNDLE_BASH_LS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_BASH_LS=ON -DTUIDE_FORCE_BUNDLED_BASH_LS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_BASH_LS=OFF -DTUIDE_FORCE_BUNDLED_BASH_LS=OFF)
  fi
  if [[ "${BUNDLE_TEXLAB}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_TEXLAB=ON)
    # Pin versions without '~' — CMake/make quote KEY=VALUE oddly and curl rejects the URL.
    args+=(-DTUIDE_CHKTEX_VERSION=1.7.10-1 -DTUIDE_PCRE2_VERSION=10.47-2)
    args+=(-DTUIDE_FORCE_BUNDLED_TEXLAB="${force}")
  else
    args+=(-DTUIDE_BUNDLE_TEXLAB=OFF -DTUIDE_FORCE_BUNDLED_TEXLAB=OFF)
  fi
  if [[ "${BUNDLE_BASH_DAP}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_BASH_DAP=ON -DTUIDE_FORCE_BUNDLED_BASH_DAP="${force}")
  else
    args+=(-DTUIDE_BUNDLE_BASH_DAP=OFF -DTUIDE_FORCE_BUNDLED_BASH_DAP=OFF)
  fi
  if [[ "${BUNDLE_RUST_ANALYZER}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_RUST_ANALYZER=ON -DTUIDE_FORCE_BUNDLED_RUST_ANALYZER="${force}")
  else
    args+=(-DTUIDE_BUNDLE_RUST_ANALYZER=OFF -DTUIDE_FORCE_BUNDLED_RUST_ANALYZER=OFF)
  fi
  if [[ "${BUNDLE_GOPLS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_GOPLS=ON -DTUIDE_FORCE_BUNDLED_GOPLS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_GOPLS=OFF -DTUIDE_FORCE_BUNDLED_GOPLS=OFF)
  fi
  if [[ "${BUNDLE_ZLS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_ZLS=ON -DTUIDE_FORCE_BUNDLED_ZLS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_ZLS=OFF -DTUIDE_FORCE_BUNDLED_ZLS=OFF)
  fi
  if [[ "${BUNDLE_FORTLS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_FORTLS=ON -DTUIDE_FORCE_BUNDLED_FORTLS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_FORTLS=OFF -DTUIDE_FORCE_BUNDLED_FORTLS=OFF)
  fi
  if [[ "${BUNDLE_LUA_LS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_LUA_LS=ON -DTUIDE_FORCE_BUNDLED_LUA_LS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_LUA_LS=OFF -DTUIDE_FORCE_BUNDLED_LUA_LS=OFF)
  fi
  if [[ "${BUNDLE_TSSERVER}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_TSSERVER=ON -DTUIDE_FORCE_BUNDLED_TSSERVER="${force}")
  else
    args+=(-DTUIDE_BUNDLE_TSSERVER=OFF -DTUIDE_FORCE_BUNDLED_TSSERVER=OFF)
  fi
  if [[ "${BUNDLE_NEOCMAKELSP}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_NEOCMAKELSP=ON -DTUIDE_FORCE_BUNDLED_NEOCMAKELSP="${force}")
  else
    args+=(-DTUIDE_BUNDLE_NEOCMAKELSP=OFF -DTUIDE_FORCE_BUNDLED_NEOCMAKELSP=OFF)
  fi
  if [[ "${BUNDLE_MAKE_LS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_MAKE_LS=ON -DTUIDE_FORCE_BUNDLED_MAKE_LS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_MAKE_LS=OFF -DTUIDE_FORCE_BUNDLED_MAKE_LS=OFF)
  fi
  if [[ "${BUNDLE_YAML_LS}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_YAML_LS=ON -DTUIDE_FORCE_BUNDLED_YAML_LS="${force}")
  else
    args+=(-DTUIDE_BUNDLE_YAML_LS=OFF -DTUIDE_FORCE_BUNDLED_YAML_LS=OFF)
  fi
  if [[ "${BUNDLE_LEMMINX}" == "1" ]]; then
    args+=(-DTUIDE_BUNDLE_LEMMINX=ON -DTUIDE_FORCE_BUNDLED_LEMMINX="${force}")
  else
    args+=(-DTUIDE_BUNDLE_LEMMINX=OFF -DTUIDE_FORCE_BUNDLED_LEMMINX=OFF)
  fi
  case "${UI_LOCALE}" in
    es) args+=(-DTUIDE_DEFAULT_UI_LOCALE=es) ;;
    *) args+=(-DTUIDE_DEFAULT_UI_LOCALE=en) ;;
  esac
  case "${EDITOR_MODE}" in
    helix) args+=(-DTUIDE_DEFAULT_HELIX_MODE=ON) ;;
    *) args+=(-DTUIDE_DEFAULT_HELIX_MODE=OFF) ;;
  esac
  printf '%s\n' "${args[@]}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -y|--yes|--non-interactive)
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --wizard|--interactive)
      SKIP_WIZARD=0
      INTERACTIVE=1
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-clangd)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_CLANGD=1
      FORCE_BUNDLED=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-clangd)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-gdb)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=1
      if [[ "${GDB_BUNDLE_KIND}" == "none" ]]; then
        GDB_BUNDLE_KIND=static
      fi
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-gdb)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-python)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=1
      if [[ "${PYTHON_BUNDLE_KIND}" == "none" ]]; then
        PYTHON_BUNDLE_KIND=lsp_min
      fi
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-python)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_LS=1
      FORCE_BUNDLED=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-bash-ls)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-texlab)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TEXLAB=1
      FORCE_BUNDLED=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-texlab)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
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
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_BASH_DAP=1
      FORCE_BUNDLED=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled-bash-dap)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --force-bundled)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-force-bundled)
      CLI_OVERRIDES_BUNDLE=1
      FORCE_BUNDLED=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-rust-analyzer)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_RUST_ANALYZER=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-rust-analyzer)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_RUST_ANALYZER=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-gopls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_GOPLS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-gopls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_GOPLS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-zls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_ZLS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-zls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_ZLS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-fortls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_FORTLS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-fortls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_FORTLS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-lua-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_LUA_LS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-lua-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_LUA_LS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-tsserver)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TSSERVER=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-tsserver)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_TSSERVER=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-neocmakelsp)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_NEOCMAKELSP=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-neocmakelsp)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_NEOCMAKELSP=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-make-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_MAKE_LS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-make-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_MAKE_LS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-yaml-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_YAML_LS=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-yaml-ls)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_YAML_LS=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --bundle-lemminx)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_LEMMINX=1
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --no-bundle-lemminx)
      CLI_OVERRIDES_BUNDLE=1
      BUNDLE_LEMMINX=0
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift
      ;;
    --target)
      if [[ $# -lt 2 ]]; then
        die "--target requiere un nombre de target CMake"
      fi
      BUILD_TARGETS+=("$2")
      SKIP_WIZARD=1
      INTERACTIVE=0
      shift 2
      ;;
    --static-libstdc++)
      STATIC_LIBSTDCXX=1
      STATIC_LIBSTDCXX_FROM_CLI=1
      shift
      ;;
    --build-backend=host|--build-backend=docker_focal|--build-backend=docker_bionic)
      BUILD_BACKEND="${1#--build-backend=}"
      BUILD_BACKEND_FROM_CLI=1
      shift
      ;;
    --build-backend)
      if [[ $# -lt 2 ]]; then
        die "--build-backend requiere host, docker_focal o docker_bionic"
      fi
      case "$2" in
        host|docker_focal|docker_bionic) BUILD_BACKEND="$2" ;;
        docker|portable) BUILD_BACKEND=docker_focal ;;
        bionic) BUILD_BACKEND=docker_bionic ;;
        *) die "--build-backend debe ser host, docker_focal o docker_bionic" ;;
      esac
      BUILD_BACKEND_FROM_CLI=1
      shift 2
      ;;
    --ui-locale=es|--ui-locale=en)
      UI_LOCALE="${1#--ui-locale=}"
      shift
      ;;
    --ui-locale)
      if [[ $# -lt 2 ]]; then
        die "--ui-locale requiere es o en"
      fi
      case "$2" in
        es|en) UI_LOCALE="$2" ;;
        *) die "--ui-locale debe ser es o en" ;;
      esac
      shift 2
      ;;
    --editor-mode=normal|--editor-mode=helix)
      EDITOR_MODE="${1#--editor-mode=}"
      shift
      ;;
    --editor-mode)
      if [[ $# -lt 2 ]]; then
        die "--editor-mode requiere normal o helix"
      fi
      case "$2" in
        normal|helix) EDITOR_MODE="$2" ;;
        *) die "--editor-mode debe ser normal o helix" ;;
      esac
      shift 2
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

if [[ "${SKIP_WIZARD}" == "0" ]] && { [[ ! -t 0 ]] || [[ ! -t 1 ]]; }; then
  # Sin TTY (p. ej. task AI con stdout pipeado): el wizard FTXUI se quedaría colgado.
  log "sin TTY: omitiendo asistente interactivo (usa .bundle-config / defaults)"
  SKIP_WIZARD=1
  INTERACTIVE=0
fi

if [[ "${SKIP_WIZARD}" == "0" ]]; then
  # TUI primero (reutiliza wizard existente o lo compila en build-wizard/).
  # Así build/ solo se configura una vez, con la selección final: sin
  # recompilar por un baile OFF→ON ni por un configure previo con otras flags.
  run_wizard
  save_bundle_config
else
  if [[ "${SKIP_WIZARD}" == "1" && "${CLI_OVERRIDES_BUNDLE}" == "0" ]]; then
    load_bundle_config
  fi
fi

maybe_delegate_portable_build

sync_gdb_bundle_flags
sync_python_bundle_flags

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
if [[ ${#BUILD_TARGETS[@]} -gt 0 ]]; then
  log "targets: ${BUILD_TARGETS[*]}"
  cmake --build "${BUILD_DIR}" -j "${JOBS}" --target "${BUILD_TARGETS[@]}"
  REQUIRE_HELLO=0
  for t in "${BUILD_TARGETS[@]}"; do
    if [[ "${t}" == "hello" ]]; then
      REQUIRE_HELLO=1
    fi
  done
else
  cmake --build "${BUILD_DIR}" -j "${JOBS}"
fi

if [[ ! -x "${BUILD_DIR}/tuide" ]]; then
  die "no se generó ${BUILD_DIR}/tuide"
fi

if [[ "${REQUIRE_HELLO}" == "1" && ! -x "${BUILD_DIR}/hello" ]]; then
  die "no se generó ${BUILD_DIR}/hello"
fi

log "listo."
log "  tuide:  ${BUILD_DIR}/tuide ($(du -h "${BUILD_DIR}/tuide" | awk '{print $1}'))"
log "  hello: ${BUILD_DIR}/hello"
if [[ "${BUNDLE_CLANGD}" == "1" ]]; then
  log "  clangd embebido: sí"
else
  log "  clangd embebido: no"
fi
if [[ "${BUNDLE_GDB}" == "1" ]]; then
  log "  gdb embebido: ${GDB_BUNDLE_KIND}"
else
  log "  gdb embebido: no"
fi
case "${PYTHON_BUNDLE_KIND}" in
  lsp_min)
    log "  python embebido: A / lsp_min"
    ;;
  full)
    log "  python embebido: B / full"
    ;;
  *)
    log "  python embebido: no"
    ;;
esac
if [[ "${BUNDLE_BASH_LS}" == "1" ]]; then
  log "  bash-language-server embebido: sí"
else
  log "  bash-language-server embebido: no"
fi
if [[ "${BUNDLE_TEXLAB}" == "1" ]]; then
  log "  texlab+chktex embebido: sí"
else
  log "  texlab embebido: no"
fi
if [[ "${BUNDLE_BASH_DAP}" == "1" ]]; then
  log "  bash DAP embebido: sí"
else
  log "  bash DAP embebido: no"
fi
if [[ "${BUNDLE_RUST_ANALYZER}" == "1" ]]; then
  log "  rust-analyzer embebido: sí"
else
  log "  rust-analyzer embebido: no"
fi
if [[ "${BUNDLE_GOPLS}" == "1" ]]; then
  log "  gopls embebido: sí"
else
  log "  gopls embebido: no"
fi
if [[ "${BUNDLE_ZLS}" == "1" ]]; then
  log "  zls embebido: sí"
else
  log "  zls embebido: no"
fi
if [[ "${BUNDLE_FORTLS}" == "1" ]]; then
  log "  fortls embebido: sí"
else
  log "  fortls embebido: no"
fi
if [[ "${BUNDLE_LUA_LS}" == "1" ]]; then
  log "  lua-language-server embebido: sí"
else
  log "  lua-language-server embebido: no"
fi
if [[ "${BUNDLE_TSSERVER}" == "1" ]]; then
  log "  typescript-ls embebido: sí"
else
  log "  typescript-ls embebido: no"
fi
if [[ "${BUNDLE_NEOCMAKELSP}" == "1" ]]; then
  log "  neocmakelsp embebido: sí"
else
  log "  neocmakelsp embebido: no"
fi
if [[ "${BUNDLE_MAKE_LS}" == "1" ]]; then
  log "  make-ls embebido: sí"
else
  log "  make-ls embebido: no"
fi
if [[ "${BUNDLE_YAML_LS}" == "1" ]]; then
  log "  yaml-language-server embebido: sí"
else
  log "  yaml-language-server embebido: no"
fi
if [[ "${BUNDLE_LEMMINX}" == "1" ]]; then
  log "  LemMinX embebido: sí"
else
  log "  LemMinX embebido: no"
fi
log "  forzar embebidos: ${FORCE_BUNDLED}"
log "  idioma por defecto: ${UI_LOCALE}"
log "  editor por defecto: ${EDITOR_MODE}"
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  log "  libstdc++ estático: sí"
fi
log ""
log "lanza con: ${ROOT}/tools/launch.sh"
