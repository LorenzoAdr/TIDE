#!/usr/bin/env bash
# Build an official slim core AppImage (glibc 2.27 / Ubuntu 18.04 baseline).
# Output: dist/tuide-${VERSION}.AppImage (+ SHA256SUMS entry).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT}/dist"
VERSION="${VERSION:-}"
SKIP_BUILD=0
PUBLISH=0
REPO="${REPO:-LorenzoAdr/TIDE}"

log() {
  printf '[build-release-appimage] %s\n' "$*"
}

die() {
  printf '[build-release-appimage] error: %s\n' "$*" >&2
  exit 1
}

usage() {
  cat <<'EOF'
Uso: tools/build-release-appimage.sh [opciones]

Compila el núcleo slim (sin bundles) con glibc ~2.27 y genera
dist/tuide-${VERSION}.AppImage para releases oficiales de GitHub.

Opciones:
  --version VER     Versión del asset (sin prefijo v). Default: tag git o $VERSION
  --skip-build      Reusar dist/tuide-x86_64-glibc2.27-static-libstdc++ existente
  --publish         Crear/actualizar release GitHub con gh (tag v${VERSION})
  --repo OWNER/REPO Default: LorenzoAdr/TIDE
  -h, --help        Esta ayuda

Variables:
  VERSION           Igual que --version
  APPIMAGETOOL      Ruta a appimagetool (o en PATH)
  JOBS              Hilos de compilación (pasa a build-portable.sh)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --version)
      [[ $# -ge 2 ]] || die "--version requiere un valor"
      VERSION="$2"
      shift 2
      ;;
    --skip-build)
      SKIP_BUILD=1
      shift
      ;;
    --publish)
      PUBLISH=1
      shift
      ;;
    --repo)
      [[ $# -ge 2 ]] || die "--repo requiere OWNER/REPO"
      REPO="$2"
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

if [[ -z "${VERSION}" ]]; then
  if git -C "${ROOT}" describe --tags --exact-match HEAD >/dev/null 2>&1; then
    VERSION="$(git -C "${ROOT}" describe --tags --exact-match HEAD)"
  elif [[ -n "${GITHUB_REF_NAME:-}" ]]; then
    VERSION="${GITHUB_REF_NAME}"
  else
    die "indica --version o publica desde un tag v* (o GITHUB_REF_NAME)"
  fi
fi

# Tag v0.1.0 → asset tuide-0.1.0.AppImage
VERSION="${VERSION#v}"
[[ -n "${VERSION}" ]] || die "VERSION vacía"

CORE_BIN="${DIST_DIR}/tuide-x86_64-glibc2.27-static-libstdc++"
OUT_APPIMAGE="${DIST_DIR}/tuide-${VERSION}.AppImage"
SUMS="${DIST_DIR}/SHA256SUMS"

ensure_appimagetool() {
  if [[ -n "${APPIMAGETOOL:-}" && -x "${APPIMAGETOOL}" ]]; then
    return 0
  fi
  if command -v appimagetool >/dev/null 2>&1; then
    return 0
  fi
  local cache="${ROOT}/.cache/appimagetool"
  local appimage="${cache}/appimagetool-x86_64.AppImage"
  mkdir -p "${cache}"
  if [[ ! -x "${appimage}" ]]; then
    log "descargando appimagetool..."
    curl -fsSL -o "${appimage}" \
      "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "${appimage}"
  fi
  # Extract so mksquashfs sits next to the binary (FUSE may be unavailable in CI).
  if [[ ! -x "${cache}/squashfs-root/AppRun" ]]; then
    log "extrayendo appimagetool (sin FUSE)..."
    (
      cd "${cache}"
      ./appimagetool-x86_64.AppImage --appimage-extract >/dev/null
    )
  fi
  export APPIMAGETOOL="${cache}/squashfs-root/AppRun"
  export PATH="${cache}/squashfs-root/usr/bin:${PATH:-/usr/bin:/bin}"
}

if [[ "${SKIP_BUILD}" != "1" ]]; then
  log "compilando núcleo slim (bionic / glibc 2.27, static-libstdc++)..."
  "${ROOT}/tools/build-portable.sh" --bionic --static-libstdc++ --slim --no-sudo-docker \
    --output "${DIST_DIR}"
else
  log "reusando binario existente (--skip-build)"
fi

[[ -x "${CORE_BIN}" ]] || die "no existe binario portable: ${CORE_BIN}"
[[ -x "${ROOT}/build/tuide" ]] || die "hace falta build/tuide para export-portable"

command -v file >/dev/null 2>&1 || die "hace falta 'file' en PATH"
command -v mksquashfs >/dev/null 2>&1 || command -v squashfs >/dev/null 2>&1 || true
# Prefer host squashfs-tools; appimagetool extract also ships mksquashfs.
if ! command -v mksquashfs >/dev/null 2>&1; then
  log "aviso: mksquashfs no está en PATH; se usará el de appimagetool si existe"
fi

ensure_appimagetool

log "empaquetando AppImage core-only → ${OUT_APPIMAGE}"
rm -f "${OUT_APPIMAGE}"
# Unset toolpacks root so export does not pick up host packs accidentally.
env -u TUIDE_TOOLPACKS_ROOT -u TUIDE_TOOLPACKS_CATALOG_URL \
  "${ROOT}/build/tuide" export-portable \
  --core-only \
  --binary "${CORE_BIN}" \
  -o "${OUT_APPIMAGE}"

[[ -f "${OUT_APPIMAGE}" ]] || die "no se generó ${OUT_APPIMAGE}"
chmod +x "${OUT_APPIMAGE}"

{
  printf '%s  %s\n' "$(sha256sum "${OUT_APPIMAGE}" | awk '{print $1}')" "tuide-${VERSION}.AppImage"
} > "${SUMS}"

log "artefacto: ${OUT_APPIMAGE} ($(du -h "${OUT_APPIMAGE}" | awk '{print $1}'))"
log "checksums: ${SUMS}"

if [[ "${PUBLISH}" == "1" ]]; then
  command -v gh >/dev/null 2>&1 || die "gh no está en PATH (necesario para --publish)"
  local_tag="v${VERSION}"
  notes="$(cat <<EOF
## tuide ${VERSION}

AppImage oficial del **núcleo** (sin LSP/DAP embebidos ni toolpacks).

- Baseline: glibc ≥ **2.27** (Ubuntu 18.04+), x86_64
- Asset: \`tuide-${VERSION}.AppImage\`

Tras instalar, añade lenguajes desde F10 → Toolpacks (catálogo GitHub).

\`\`\`bash
chmod +x tuide-${VERSION}.AppImage
./tuide-${VERSION}.AppImage
\`\`\`
EOF
)"
  if gh release view "${local_tag}" --repo "${REPO}" >/dev/null 2>&1; then
    log "actualizando assets del release ${local_tag}..."
    gh release upload "${local_tag}" "${OUT_APPIMAGE}" "${SUMS}" \
      --repo "${REPO}" --clobber
  else
    log "creando release ${local_tag}..."
    gh release create "${local_tag}" "${OUT_APPIMAGE}" "${SUMS}" \
      --repo "${REPO}" \
      --title "tuide ${VERSION}" \
      --notes "${notes}"
  fi
  log "publicado: https://github.com/${REPO}/releases/tag/${local_tag}"
fi

log "listo."
