#!/usr/bin/env bash
# Instala el ejecutable en /usr/opt/tide y registra el alias "tide" en el shell.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_BIN="${ROOT}/build/tgdb"
INSTALL_DIR="/usr/opt"
INSTALL_BIN="${INSTALL_DIR}/tide"
MARKER_BEGIN="# >>> tide (tgdb) >>>"
MARKER_END="# <<< tide (tgdb) <<<"
ALIAS_LINE='alias tide="/usr/opt/tide"'

usage() {
  cat <<EOF
Uso: $(basename "$0") [opciones]

Instala el IDE en ${INSTALL_BIN} y añade el alias "tide" al shell.

Opciones:
  --no-build   No compilar; usar el binario existente en build/tgdb
  -h, --help   Muestra esta ayuda

Requiere permisos de administrador (sudo) para copiar en ${INSTALL_DIR}.
EOF
}

die() {
  printf '[install] error: %s\n' "$*" >&2
  exit 1
}

log() {
  printf '[install] %s\n' "$*"
}

need_build=true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build)
      need_build=false
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      die "opción desconocida: $1 (usa --help)"
      ;;
  esac
done

if [[ "${need_build}" == true ]]; then
  log "compilando…"
  "${ROOT}/tools/compile.sh"
fi

[[ -x "${BUILD_BIN}" ]] || die "no se encontró el ejecutable: ${BUILD_BIN}"

if [[ "$(id -u)" -ne 0 ]]; then
  SUDO=(sudo)
else
  SUDO=()
fi

log "instalando en ${INSTALL_BIN}"
"${SUDO[@]}" mkdir -p "${INSTALL_DIR}"
"${SUDO[@]}" install -m 755 "${BUILD_BIN}" "${INSTALL_BIN}"

pick_shell_rc() {
  # Preferir .bashrc (shells interactivos). Si no existe, usar .profile.
  if [[ -f "${HOME}/.bashrc" ]]; then
    printf '%s\n' "${HOME}/.bashrc"
    return
  fi
  if [[ -f "${HOME}/.profile" ]]; then
    printf '%s\n' "${HOME}/.profile"
    return
  fi
  printf '%s\n' "${HOME}/.bashrc"
}

install_alias_block() {
  local rc="$1"
  touch "${rc}"

  if grep -Fq "${MARKER_BEGIN}" "${rc}" 2>/dev/null; then
    # Actualizar bloque existente (idempotente).
    local tmp
    tmp="$(mktemp)"
    awk -v begin="${MARKER_BEGIN}" -v end="${MARKER_END}" -v alias="${ALIAS_LINE}" '
      $0 == begin { skip=1; print begin; print alias; print end; next }
      $0 == end { skip=0; next }
      skip == 0 { print }
    ' "${rc}" > "${tmp}"
    mv "${tmp}" "${rc}"
    log "alias actualizado en ${rc}"
  else
    {
      printf '\n%s\n' "${MARKER_BEGIN}"
      printf '%s\n' "${ALIAS_LINE}"
      printf '%s\n' "${MARKER_END}"
    } >> "${rc}"
    log "alias añadido en ${rc}"
  fi
}

RC="$(pick_shell_rc)"
install_alias_block "${RC}"

# En sistemas donde el login shell solo lee .profile, asegurar el alias ahí también.
if [[ "${RC}" != "${HOME}/.profile" && -f "${HOME}/.profile" ]]; then
  if ! grep -Fq "${MARKER_BEGIN}" "${HOME}/.profile" 2>/dev/null; then
    install_alias_block "${HOME}/.profile"
  fi
fi

log "listo."
log "  ejecutable: ${INSTALL_BIN}"
log "  alias:      tide"
log ""
log "Activa el alias en esta sesión con:"
log "  source ${RC}"
log "o abre una terminal nueva y ejecuta:"
log "  tide"
