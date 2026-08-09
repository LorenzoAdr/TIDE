#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/dist"
IMAGE_TAG="tuide-portable-builder"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
USE_BIONIC=0
STATIC_LIBSTDCXX=0
SKIP_VERIFY=0
SKIP_BUILD_IMAGE=0
SLIM=0
MAX_GLIBC="2.31"
# 0=auto, 1=forzar sudo docker, 2=nunca sudo
DOCKER_SUDO="${TUIDE_DOCKER_SUDO:-0}"

log() {
  printf '[build-portable] %s\n' "$*"
}

die() {
  printf '[build-portable] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Uso: tools/build-portable.sh [opciones]

Compila tuide dentro de Docker usando una distro con glibc antigua para
maximizar compatibilidad en runtime. Respeta la selección de .bundle-config
(mismo contenido que el asistente TUI / compile.sh -y).

Opciones:
  --bionic              Usar Ubuntu 18.04 (glibc ~2.27) en lugar de 20.04 (~2.31)
  --static-libstdc++    Pasar --static-libstdc++ a compile.sh
  --slim                Nucleo sin bundles (ignora .bundle-config; release AppImage)
  --jobs N              Hilos de compilación (default: nproc)
  --output DIR          Directorio de salida (default: dist/)
  --max-glibc VERSION   Umbral para verify-glibc.sh (default: 2.31, bionic: 2.27)
  --skip-verify         No ejecutar tools/verify-glibc.sh
  --skip-image-build    Reusar imagen Docker existente
  --sudo-docker         Usar «sudo docker» (p. ej. sin grupo docker)
  --no-sudo-docker      No ofrecer ni usar sudo si falla el permiso
  -h, --help            Mostrar esta ayuda

Variables de entorno:
  TUIDE_DOCKER_SUDO=1   Igual que --sudo-docker
  TUIDE_DOCKER_SUDO=0   Auto: preguntar si hay permission denied (default)
  JOBS                  Hilos de compilación

Ejemplos:
  ./tools/compile.sh                      # TUI → Compilación: Docker …
  ./tools/build-portable.sh               # reusa .bundle-config (focal)
  ./tools/build-portable.sh --sudo-docker
  ./tools/build-portable.sh --bionic --static-libstdc++
EOF
}

check_command() {
  command -v "$1" >/dev/null 2>&1 || die "no se encontró '$1' en PATH (¿Docker instalado?)"
}

dockerfile_for_variant() {
  if [[ "${USE_BIONIC}" == "1" ]]; then
    printf '%s/docker/Dockerfile.portable.bionic' "${ROOT}"
  else
    printf '%s/docker/Dockerfile.portable' "${ROOT}"
  fi
}

glibc_label() {
  if [[ "${USE_BIONIC}" == "1" ]]; then
    printf '2.27'
  else
    printf '2.31'
  fi
}

# Ejecuta docker, con sudo si DOCKER_SUDO=1.
docker_cmd() {
  if [[ "${DOCKER_SUDO}" == "1" ]]; then
    sudo docker "$@"
  else
    docker "$@"
  fi
}

is_permission_denied_docker_error() {
  local err="$1"
  grep -qiE 'permission denied|access denied' <<<"${err}"
}

prompt_yes_no() {
  local prompt="$1"
  local reply=""
  if [[ ! -t 0 ]] || [[ ! -t 2 ]]; then
    return 1
  fi
  printf '%s' "${prompt}" >&2
  read -r reply || return 1
  case "${reply}" in
    ""|s|S|y|Y|si|Sí|sí|SI|yes|YES) return 0 ;;
    *) return 1 ;;
  esac
}

# Comprueba el daemon; ante permission denied ofrece reintentar con sudo.
ensure_docker_usable() {
  check_command docker

  local err=""
  if err="$(docker info 2>&1)"; then
    # Preferir docker sin sudo si el usuario ya tiene acceso.
    DOCKER_SUDO=0
    return 0
  fi

  if [[ "${DOCKER_SUDO}" == "2" ]]; then
    die "Docker no responde: ${err}"
  fi

  if [[ "${DOCKER_SUDO}" == "1" ]] || is_permission_denied_docker_error "${err}"; then
    if [[ "${DOCKER_SUDO}" != "1" ]]; then
      log "Docker: sin permiso en /var/run/docker.sock"
      if ! prompt_yes_no "¿Reintentar esta compilación con sudo docker? [S/n] "; then
        die "sin acceso a Docker. Añade tu usuario al grupo docker, o relanza con --sudo-docker"
      fi
      DOCKER_SUDO=1
    fi
    check_command sudo
    log "usando sudo docker (pedirá la contraseña si hace falta)…"
    if ! docker_cmd info >/dev/null 2>&1; then
      die "sudo docker tampoco responde (¿daemon arrancado?)"
    fi
    return 0
  fi

  die "Docker no responde (¿daemon arrancado?): ${err}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bionic)
      USE_BIONIC=1
      MAX_GLIBC="2.27"
      IMAGE_TAG="tuide-portable-builder-bionic"
      shift
      ;;
    --static-libstdc++)
      STATIC_LIBSTDCXX=1
      shift
      ;;
    --slim)
      SLIM=1
      shift
      ;;
    --jobs)
      [[ $# -ge 2 ]] || die "--jobs requiere un número"
      JOBS="$2"
      shift 2
      ;;
    --output)
      [[ $# -ge 2 ]] || die "--output requiere un directorio"
      DIST_DIR="$2"
      shift 2
      ;;
    --max-glibc)
      [[ $# -ge 2 ]] || die "--max-glibc requiere un valor"
      MAX_GLIBC="$2"
      shift 2
      ;;
    --skip-verify)
      SKIP_VERIFY=1
      shift
      ;;
    --skip-image-build)
      SKIP_BUILD_IMAGE=1
      shift
      ;;
    --sudo-docker)
      DOCKER_SUDO=1
      shift
      ;;
    --no-sudo-docker)
      DOCKER_SUDO=2
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

# Normalizar TUIDE_DOCKER_SUDO si vino como true/yes
case "${DOCKER_SUDO}" in
  1|true|TRUE|yes|YES) DOCKER_SUDO=1 ;;
  2|never|NEVER) DOCKER_SUDO=2 ;;
  *) DOCKER_SUDO=0 ;;
esac

ensure_docker_usable

DOCKERFILE="$(dockerfile_for_variant)"
[[ -f "${DOCKERFILE}" ]] || die "no existe ${DOCKERFILE}"

if [[ "${SKIP_BUILD_IMAGE}" == "0" ]]; then
  log "construyendo imagen ${IMAGE_TAG} (${DOCKERFILE})..."
  docker_cmd build -f "${DOCKERFILE}" -t "${IMAGE_TAG}" "${ROOT}"
else
  log "reusando imagen ${IMAGE_TAG}"
fi

# -y reutiliza .bundle-config (selección del wizard). No forzar packs.
# --slim: un --no-bundle-* activa CLI_OVERRIDES_BUNDLE y deja todos los bundles en 0.
# TUIDE_IN_PORTABLE_CONTAINER evita que compile.sh vuelva a lanzar Docker.
COMPILE_ARGS=(-y --build-backend=host)
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  COMPILE_ARGS+=(--static-libstdc++)
fi
if [[ "${SLIM}" == "1" ]]; then
  COMPILE_ARGS+=(--no-bundle-clangd)
  log "modo slim: sin componentes embebidos (release core)"
fi

log "compilando en contenedor (${JOBS} hilos)..."
docker_cmd run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e JOBS="${JOBS}" \
  -e CMAKE_BUILD_TYPE=Release \
  -e TUIDE_IN_PORTABLE_CONTAINER=1 \
  -v "${ROOT}:/src" \
  -w /src \
  "${IMAGE_TAG}" \
  ./tools/compile.sh "${COMPILE_ARGS[@]}"

[[ -x "${ROOT}/build/tuide" ]] || die "no se generó build/tuide"

if [[ "${SKIP_VERIFY}" == "0" ]]; then
  log "verificando dependencias de runtime..."
  "${ROOT}/tools/verify-glibc.sh" --max-glibc "${MAX_GLIBC}" "${ROOT}/build/tuide"
fi

mkdir -p "${DIST_DIR}"
suffix="x86_64-glibc$(glibc_label)"
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  suffix="${suffix}-static-libstdc++"
fi
OUT="${DIST_DIR}/tuide-${suffix}"
cp -f "${ROOT}/build/tuide" "${OUT}"
chmod +x "${OUT}"

log "artefacto: ${OUT} ($(du -h "${OUT}" | awk '{print $1}'))"
if [[ -f "${ROOT}/build/portable-report.txt" ]]; then
  cp -f "${ROOT}/build/portable-report.txt" "${DIST_DIR}/portable-report.txt"
  log "informe: ${DIST_DIR}/portable-report.txt"
fi
log "listo."
