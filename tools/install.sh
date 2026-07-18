#!/usr/bin/env bash
# Instala el ejecutable en /usr/opt/tuide y registra el alias "tuide" en el shell.
# No compila: usa el binario ya generado en build/tuide (o TUIDE_BIN).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_BIN="${TUIDE_BIN:-${ROOT}/build/tuide}"
INSTALL_DIR="/usr/opt"
INSTALL_BIN="${INSTALL_DIR}/tuide"
MARKER_BEGIN="# >>> tuide >>>"
MARKER_END="# <<< tuide <<<"
ALIAS_LINE='alias tuide="/usr/opt/tuide"'

usage() {
  cat <<EOF
Uso: $(basename "$0") [opciones]

Copia el binario a ${INSTALL_BIN} y añade el alias "tuide" a ~/.bashrc.
No compila; el binario debe existir de antemano (p. ej. tras tools/compile.sh).

Opciones:
  -h, --help   Muestra esta ayuda

Variables de entorno:
  TUIDE_BIN     Ruta al ejecutable a instalar (default: build/tuide)

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

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h | --help)
      usage
      exit 0
      ;;
    *)
      die "opción desconocida: $1 (usa --help)"
      ;;
  esac
done

[[ -x "${BUILD_BIN}" ]] || die "no se encontró el ejecutable: ${BUILD_BIN} (compila antes con tools/compile.sh)"

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
log "  alias:      tuide"
log ""
log "Activa el alias en esta sesión con:"
log "  source ${RC}"
log "o abre una terminal nueva y ejecuta:"
log "  tuide"
