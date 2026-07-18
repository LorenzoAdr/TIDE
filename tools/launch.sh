#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
TUIDE="${BUILD_DIR}/tuide"
HELLO="${BUILD_DIR}/hello"

usage() {
  cat <<EOF
Uso: $(basename "$0") [opciones] [<dir>|<archivo>|<programa>] [--args arg1 arg2 ...]

Opciones:
  --cwd <dir>           Directorio raíz del workspace (por defecto: raíz del proyecto)
  --attach <pid>        Adjuntar a un proceso en ejecución
  --target <host:puerto>  Adjuntar a gdbserver (ej. localhost:1234)
  -h, --help            Muestra esta ayuda

Un directorio abre el workspace; un archivo de código abre su carpeta con el archivo cargado.

Ejemplos:
  $(basename "$0")
  $(basename "$0") .
  $(basename "$0") ..
  $(basename "$0") src/main.cpp
  $(basename "$0") ../build/hello
  $(basename "$0") --cwd .. ../build/hello
  $(basename "$0") --attach 12345 ../build/hello
  $(basename "$0") --target localhost:1234 ../build/hello

Nota: <programa> es el ejecutable con símbolos (-g), incluso en attach.
EOF
}

die() {
  printf '[launch] error: %s\n' "$*" >&2
  exit 1
}

resolve_path() {
  local path="$1"
  if [[ -z "${path}" ]]; then
    die "ruta vacía"
  fi
  if [[ "${path}" = /* ]]; then
    printf '%s\n' "${path}"
    return
  fi
  realpath -e "${path}" 2>/dev/null || realpath -m "${PWD}/${path}"
}

is_existing_directory() {
  local path="$1"
  [[ -d "$(resolve_path "${path}")" ]]
}

path_looks_like_source_or_text() {
  local path="$1"
  case "${path##*.}" in
    cpp|cxx|cc|c|h|hpp|hxx|hh|inl|md|txt|json|yaml|yml|xml|cmake|toml|rs|py|sh|bash|s|asm|vert|frag|glsl)
      return 0
      ;;
  esac
  [[ "${path}" == *.* ]] || return 1
  return 1
}

is_ide_open_file() {
  local path="$1"
  local resolved
  resolved="$(resolve_path "${path}")"
  [[ -f "${resolved}" ]] || return 1
  if path_looks_like_source_or_text "${path}"; then
    return 0
  fi
  [[ ! -x "${resolved}" ]]
}

if [[ ! -x "${TUIDE}" ]]; then
  die "no existe ${TUIDE}. Ejecuta primero: ${ROOT}/tools/compile.sh"
fi

if ! command -v gdb >/dev/null 2>&1; then
  printf '[launch] aviso: gdb no está instalado; el IDE arrancará sin depuración\n' >&2
elif ! gdb -i=dap -ex quit >/dev/null 2>&1; then
  printf '[launch] aviso: GDB no soporta DAP (gdb -i=dap); el IDE arrancará sin depuración\n' >&2
fi

export TERM="${TERM:-xterm-256color}"
export COLORTERM="${COLORTERM:-truecolor}"

if [[ $# -eq 0 ]]; then
  exec "${TUIDE}"
fi

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

WORKSPACE="${ROOT}"
PROGRAM=""
IDE_FILE=""
EXPLICIT_WORKSPACE=false
ATTACH_PID=""
ATTACH_TARGET=""
PROGRAM_ARGS=()
i=1
while [[ $i -le $# ]]; do
  arg="${!i}"
  case "${arg}" in
    -h|--help)
      usage
      exit 0
      ;;
    --cwd)
      i=$((i + 1))
      [[ $i -le $# ]] || die "falta valor para --cwd"
      WORKSPACE="$(resolve_path "${!i}")"
      EXPLICIT_WORKSPACE=true
      ;;
    --attach)
      i=$((i + 1))
      [[ $i -le $# ]] || die "falta PID para --attach"
      ATTACH_PID="${!i}"
      ;;
    --target)
      i=$((i + 1))
      [[ $i -le $# ]] || die "falta host:puerto para --target"
      ATTACH_TARGET="${!i}"
      ;;
    --args)
      i=$((i + 1))
      while [[ $i -le $# ]]; do
        PROGRAM_ARGS+=("${!i}")
        i=$((i + 1))
      done
      break
      ;;
    --*)
      die "opción desconocida: ${arg} (usa --help)"
      ;;
    *)
      if is_existing_directory "${arg}"; then
        WORKSPACE="$(resolve_path "${arg}")"
        EXPLICIT_WORKSPACE=true
      elif is_ide_open_file "${arg}"; then
        [[ -z "${IDE_FILE}" ]] || die "demasiados archivos posicionales"
        IDE_FILE="${arg}"
      elif [[ -z "${PROGRAM}" ]]; then
        PROGRAM="${arg}"
      else
        die "demasiados argumentos posicionales. Usa --args para argumentos del programa."
      fi
      ;;
  esac
  i=$((i + 1))
done

if [[ -z "${PROGRAM}" ]]; then
  if [[ -n "${IDE_FILE}" ]]; then
    if [[ -n "${ATTACH_PID}" || -n "${ATTACH_TARGET}" ]]; then
      die "attach/target requiere un programa ejecutable"
    fi
    IDE_FILE="$(resolve_path "${IDE_FILE}")"
    [[ -f "${IDE_FILE}" ]] || die "archivo no encontrado: ${IDE_FILE}"
    PASSTHROUGH=()
    if [[ "${EXPLICIT_WORKSPACE}" == true ]]; then
      [[ -e "${WORKSPACE}" ]] || die "workspace no existe: ${WORKSPACE}"
      [[ -d "${WORKSPACE}" ]] || die "workspace debe ser un directorio: ${WORKSPACE}"
      PASSTHROUGH+=(--cwd "${WORKSPACE}")
    fi
    PASSTHROUGH+=("${IDE_FILE}")
    exec "${TUIDE}" "${PASSTHROUGH[@]}"
  fi
  [[ -e "${WORKSPACE}" ]] || die "workspace no existe: ${WORKSPACE}"
  [[ -d "${WORKSPACE}" ]] || die "workspace debe ser un directorio: ${WORKSPACE}"
  exec "${TUIDE}" --cwd "${WORKSPACE}"
fi

if [[ -n "${ATTACH_PID}" && -n "${ATTACH_TARGET}" ]]; then
  die "usa solo uno: --attach o --target"
fi

PROGRAM="$(resolve_path "${PROGRAM}")"

[[ -e "${WORKSPACE}" ]] || die "workspace no existe: ${WORKSPACE}"
[[ -d "${WORKSPACE}" ]] || die "--cwd debe ser un directorio: ${WORKSPACE}"
[[ -f "${PROGRAM}" ]] || die "programa no encontrado: ${PROGRAM}"
[[ -x "${PROGRAM}" ]] || die "programa no ejecutable: ${PROGRAM}"

PASSTHROUGH=(--cwd "${WORKSPACE}")
if [[ -n "${ATTACH_PID}" ]]; then
  PASSTHROUGH+=(--attach "${ATTACH_PID}")
  if [[ "$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 1)" != "0" ]]; then
    cat <<EOF >&2
[launch] aviso: yama.ptrace_scope no es 0; attach a procesos externos puede fallar.
         hello recompilado incluye PR_SET_PTRACER. Si gdb da error ptrace:
           sudo sysctl kernel.yama.ptrace_scope=0
         o depura sin attach: ${ROOT}/tools/launch.sh ${PROGRAM}

EOF
  fi
elif [[ -n "${ATTACH_TARGET}" ]]; then
  PASSTHROUGH+=(--target "${ATTACH_TARGET}")
fi
PASSTHROUGH+=("${PROGRAM}")

if [[ ${#PROGRAM_ARGS[@]} -gt 0 ]]; then
  PASSTHROUGH+=(--args "${PROGRAM_ARGS[@]}")
fi

exec "${TUIDE}" "${PASSTHROUGH[@]}"
