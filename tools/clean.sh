#!/usr/bin/env bash
# Elimina árboles de build CMake y restos de compilaciones previas.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DRY_RUN=0
KEEP_BUNDLES=0
KEEP_CONFIG=1
YES=0

log() {
  printf '[clean] %s\n' "$*"
}

die() {
  printf '[clean] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Uso: tools/clean.sh [opciones]

Borra el directorio de build y la información de compilaciones previas
(árboles CMake auxiliares, compile_commands.json, caches, dist/, bundles
descargados, artefactos de examples/).

Opciones:
  -n, --dry-run       Solo listar lo que se eliminaría
  -y, --yes           No pedir confirmación
  --keep-bundles      Conservar third_party/bundled/ (tarballs/binarios embebidos)
  --purge-config      También borrar .bundle-config
  -h, --help          Mostrar esta ayuda

Ejemplos:
  ./tools/clean.sh
  ./tools/clean.sh -y
  ./tools/clean.sh --dry-run
  ./tools/clean.sh --purge-config -y
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--dry-run)
      DRY_RUN=1
      shift
      ;;
    -y|--yes)
      YES=1
      shift
      ;;
    --keep-bundles)
      KEEP_BUNDLES=1
      shift
      ;;
    --purge-config)
      KEEP_CONFIG=0
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

# Rutas absolutas candidatas (solo se borran si existen).
candidates=()

# Árboles CMake en la raíz del repo (build/, build-wizard/, …).
shopt -s nullglob
for d in "${ROOT}"/build "${ROOT}"/build-*; do
  [[ -e "$d" ]] || continue
  [[ -d "$d" ]] || continue
  candidates+=("$d")
done
shopt -u nullglob

# Symlink / copia de compile_commands en la raíz.
if [[ -e "${ROOT}/compile_commands.json" || -L "${ROOT}/compile_commands.json" ]]; then
  candidates+=("${ROOT}/compile_commands.json")
fi

# Caché local del proyecto (clangd, etc.).
if [[ -d "${ROOT}/.cache" ]]; then
  candidates+=("${ROOT}/.cache")
fi

# Salida de build-portable.sh.
if [[ -d "${ROOT}/dist" ]]; then
  candidates+=("${ROOT}/dist")
fi

# Bundles descargados en tiempo de compilación.
if [[ "${KEEP_BUNDLES}" -eq 0 && -d "${ROOT}/third_party/bundled" ]]; then
  candidates+=("${ROOT}/third_party/bundled")
fi

# Config de componentes embebidos (opcional).
if [[ "${KEEP_CONFIG}" -eq 0 && -f "${ROOT}/.bundle-config" ]]; then
  candidates+=("${ROOT}/.bundle-config")
fi

# Artefactos de ejemplos (p. ej. cargo target/).
if [[ -d "${ROOT}/examples/target" ]]; then
  candidates+=("${ROOT}/examples/target")
fi

# Caché de Python de tools/.
if [[ -d "${ROOT}/tools/__pycache__" ]]; then
  candidates+=("${ROOT}/tools/__pycache__")
fi

# Objetos sueltos en la raíz (gitignore: *.o *.a).
shopt -s nullglob
for f in "${ROOT}"/*.o "${ROOT}"/*.a; do
  [[ -e "$f" ]] || continue
  candidates+=("$f")
done
shopt -u nullglob

if [[ ${#candidates[@]} -eq 0 ]]; then
  log "nada que limpiar"
  exit 0
fi

log "se eliminará:"
for path in "${candidates[@]}"; do
  if [[ -d "$path" ]]; then
    size="$(du -sh "$path" 2>/dev/null | awk '{print $1}')"
    printf '  %s  (%s)\n' "${path#"${ROOT}"/}" "${size:-?}"
  else
    printf '  %s\n' "${path#"${ROOT}"/}"
  fi
done

if [[ "${DRY_RUN}" -eq 1 ]]; then
  log "dry-run: no se borró nada"
  exit 0
fi

if [[ "${YES}" -eq 0 ]]; then
  printf '[clean] ¿continuar? [y/N] '
  read -r answer
  case "${answer}" in
    y|Y|yes|YES) ;;
    *)
      log "cancelado"
      exit 0
      ;;
  esac
fi

for path in "${candidates[@]}"; do
  rm -rf -- "$path"
  log "eliminado: ${path#"${ROOT}"/}"
done

log "listo"
