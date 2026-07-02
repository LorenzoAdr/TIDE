#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/dist"
IMAGE_TAG="tgdb-portable-builder"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
USE_BIONIC=0
STATIC_LIBSTDCXX=0
SKIP_VERIFY=0
SKIP_BUILD_IMAGE=0
MAX_GLIBC="2.31"

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

Compila tgdb con pack completo (clangd + gdb embebidos) dentro de Docker
usando una distro con glibc antigua para maximizar compatibilidad en runtime.

Opciones:
  --bionic              Usar Ubuntu 18.04 (glibc ~2.27) en lugar de 20.04 (~2.31)
  --static-libstdc++    Pasar --static-libstdc++ a compile.sh
  --jobs N              Hilos de compilación (default: nproc)
  --output DIR          Directorio de salida (default: dist/)
  --max-glibc VERSION   Umbral para verify-glibc.sh (default: 2.31, bionic: 2.27)
  --skip-verify         No ejecutar tools/verify-glibc.sh
  --skip-image-build    Reusar imagen Docker existente
  -h, --help            Mostrar esta ayuda

Ejemplos:
  ./tools/build-portable.sh
  ./tools/build-portable.sh --static-libstdc++
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

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bionic)
      USE_BIONIC=1
      MAX_GLIBC="2.27"
      IMAGE_TAG="tgdb-portable-builder-bionic"
      shift
      ;;
    --static-libstdc++)
      STATIC_LIBSTDCXX=1
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
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "opción desconocida: $1 (usa --help)"
      ;;
  esac
done

check_command docker

DOCKERFILE="$(dockerfile_for_variant)"
[[ -f "${DOCKERFILE}" ]] || die "no existe ${DOCKERFILE}"

if [[ "${SKIP_BUILD_IMAGE}" == "0" ]]; then
  log "construyendo imagen ${IMAGE_TAG} (${DOCKERFILE})..."
  docker build -f "${DOCKERFILE}" -t "${IMAGE_TAG}" "${ROOT}"
else
  log "reusando imagen ${IMAGE_TAG}"
fi

COMPILE_ARGS=(
  -y
  --bundle-clangd --force-bundled-clangd
  --bundle-gdb --force-bundled-gdb
)
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  COMPILE_ARGS+=(--static-libstdc++)
fi

log "compilando en contenedor (${JOBS} hilos)..."
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -e HOME=/tmp \
  -e JOBS="${JOBS}" \
  -e CMAKE_BUILD_TYPE=Release \
  -v "${ROOT}:/src" \
  -w /src \
  "${IMAGE_TAG}" \
  ./tools/compile.sh "${COMPILE_ARGS[@]}"

[[ -x "${ROOT}/build/tgdb" ]] || die "no se generó build/tgdb"

if [[ "${SKIP_VERIFY}" == "0" ]]; then
  log "verificando dependencias de runtime..."
  "${ROOT}/tools/verify-glibc.sh" --max-glibc "${MAX_GLIBC}" "${ROOT}/build/tgdb"
fi

mkdir -p "${DIST_DIR}"
suffix="x86_64-glibc$(glibc_label)"
if [[ "${STATIC_LIBSTDCXX}" == "1" ]]; then
  suffix="${suffix}-static-libstdc++"
fi
OUT="${DIST_DIR}/tgdb-${suffix}"
cp -f "${ROOT}/build/tgdb" "${OUT}"
chmod +x "${OUT}"

log "artefacto: ${OUT} ($(du -h "${OUT}" | awk '{print $1}'))"
if [[ -f "${ROOT}/build/portable-report.txt" ]]; then
  cp -f "${ROOT}/build/portable-report.txt" "${DIST_DIR}/portable-report.txt"
  log "informe: ${DIST_DIR}/portable-report.txt"
fi
log "listo."
