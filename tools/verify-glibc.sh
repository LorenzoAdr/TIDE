#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TUIDE_BIN="${TUIDE_BIN:-${ROOT}/build/tuide}"
REPORT="${REPORT:-${ROOT}/build/portable-report.txt}"
MAX_GLIBC="${MAX_GLIBC:-}"
FAIL_ON_EXCEED=1

log() {
  printf '[verify-glibc] %s\n' "$*"
}

die() {
  printf '[verify-glibc] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Uso: tools/verify-glibc.sh [opciones] [ruta/a/tuide]

Analiza dependencias de runtime (ldd + símbolos GLIBC/GLIBCXX) de tuide y,
si existen, los binarios embebidos clangd/gdb en build/generated/bundled/.

Opciones:
  --max-glibc VERSION   Fallar si tuide requiere glibc > VERSION (ej. 2.31)
  --no-fail             Solo informar; no salir con error
  --report PATH         Ruta del informe (default: build/portable-report.txt)
  -h, --help            Mostrar esta ayuda
EOF
}

version_gt() {
  local a="${1#GLIBC_}"
  local b="${2#GLIBC_}"
  local winner
  winner="$(printf '%s\n%s\n' "${a}" "${b}" | sort -t. -k1,1n -k2,2n -k3,3n | tail -1)"
  [[ "${winner}" == "${a}" && "${a}" != "${b}" ]]
}

max_symbol_version() {
  local bin="$1"
  local prefix="$2"
  objdump -T "${bin}" 2>/dev/null \
    | sed -n "s/.*${prefix}_\\([0-9.]*\\).*/\\1/p" \
    | sort -t. -k1,1n -k2,2n -k3,3n \
    | tail -1
}

append_report() {
  printf '%s\n' "$@" >> "${REPORT}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --max-glibc)
      [[ $# -ge 2 ]] || die "--max-glibc requiere un valor"
      MAX_GLIBC="$2"
      shift 2
      ;;
    --no-fail)
      FAIL_ON_EXCEED=0
      shift
      ;;
    --report)
      [[ $# -ge 2 ]] || die "--report requiere una ruta"
      REPORT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    -*)
      die "opción desconocida: $1"
      ;;
    *)
      TUIDE_BIN="$1"
      shift
      ;;
  esac
done

[[ -f "${TUIDE_BIN}" ]] || die "no existe: ${TUIDE_BIN}"
command -v objdump >/dev/null 2>&1 || die "objdump no encontrado en PATH"
command -v ldd >/dev/null 2>&1 || die "ldd no encontrado en PATH"

mkdir -p "$(dirname "${REPORT}")"
: > "${REPORT}"

append_report "# Informe portable tuide"
append_report "fecha: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
append_report "binario: ${TUIDE_BIN}"
append_report ""

tuide_max_glibc="$(max_symbol_version "${TUIDE_BIN}" "GLIBC")"
tuide_max_glibcxx="$(max_symbol_version "${TUIDE_BIN}" "GLIBCXX")"
tuide_ldd="$(ldd "${TUIDE_BIN}" 2>/dev/null || true)"

append_report "## tuide"
append_report "GLIBC máx: ${tuide_max_glibc:-ninguno}"
append_report "GLIBCXX máx: ${tuide_max_glibcxx:-ninguno}"
append_report ""
append_report "### ldd"
append_report "${tuide_ldd}"
append_report ""

log "tuide: GLIBC máx ${tuide_max_glibc:-desconocido}, GLIBCXX máx ${tuide_max_glibcxx:-desconocido}"
log "tuide ldd:"
printf '%s\n' "${tuide_ldd}" | sed 's/^/  /'

clangd="$(find "${ROOT}/build/generated/bundled" -path '*/clangd_payload/bin/clangd' -type f 2>/dev/null | head -n1 || true)"
append_report "## clangd"
if [[ -z "${clangd}" ]]; then
  log "clangd embebido: no encontrado (omitido)"
  append_report "no encontrado en build/generated/bundled"
else
  clangd_max_glibc="$(max_symbol_version "${clangd}" "GLIBC")"
  clangd_ldd="$(ldd "${clangd}" 2>/dev/null || true)"
  log "clangd: GLIBC máx ${clangd_max_glibc:-desconocido}"
  append_report "GLIBC máx: ${clangd_max_glibc:-ninguno}"
  append_report ""
  append_report "### ldd"
  append_report "${clangd_ldd}"
  if [[ -n "${clangd_max_glibc}" ]] && version_gt "GLIBC_${clangd_max_glibc}" "GLIBC_2.18"; then
    die "clangd requiere GLIBC_${clangd_max_glibc} (> 2.18 esperado para release oficial)"
  fi
fi
append_report ""

gdb="$(find "${ROOT}/build/generated/bundled" -path '*/gdb_payload/bin/gdb' -type f 2>/dev/null | head -n1 || true)"
append_report "## gdb"
if [[ -z "${gdb}" ]]; then
  log "gdb embebido: no encontrado (omitido)"
  append_report "no encontrado en build/generated/bundled"
else
  gdb_ldd="$(ldd "${gdb}" 2>&1 || true)"
  log "gdb: ${gdb_ldd}"
  append_report "${gdb_ldd}"
  if [[ "${gdb_ldd}" != *"statically linked"* ]]; then
    die "gdb embebido no está enlazado estáticamente"
  fi
fi

if [[ -n "${MAX_GLIBC}" && -n "${tuide_max_glibc}" ]]; then
  if version_gt "GLIBC_${tuide_max_glibc}" "GLIBC_${MAX_GLIBC}"; then
    msg="tuide requiere GLIBC_${tuide_max_glibc} (> ${MAX_GLIBC} permitido)"
    if [[ "${FAIL_ON_EXCEED}" == "1" ]]; then
      die "${msg}"
    fi
    log "aviso: ${msg}"
  else
    log "GLIBC_${tuide_max_glibc} <= ${MAX_GLIBC} (OK)"
  fi
fi

log "informe: ${REPORT}"
