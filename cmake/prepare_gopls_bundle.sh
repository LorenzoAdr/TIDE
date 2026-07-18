#!/usr/bin/env bash
# Empaqueta gopls vía `go install`.
# Si no hay `go` en PATH, descarga un toolchain oficial de Go solo para el build
# (no se embebe Go; solo el binario gopls).
set -euo pipefail

die() { printf 'prepare_gopls_bundle: error: %s\n' "$*" >&2; exit 1; }

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

# Descarga atómica a dest (parcial + mv). Reintenta si el fichero es demasiado pequeño.
download_file() {
  local url="$1"
  local dest="$2"
  local min_bytes="${3:-0}"
  local partial="${dest}.partial"
  local size=0

  mkdir -p "$(dirname "${dest}")"

  if [[ -f "${dest}" ]]; then
    size="$(wc -c < "${dest}" | tr -d ' ')"
    if [[ "${min_bytes}" -gt 0 && "${size}" -lt "${min_bytes}" ]]; then
      printf 'prepare_gopls_bundle: caché incompleta (%s bytes < %s); redescargando\n' \
        "${size}" "${min_bytes}"
      rm -f "${dest}"
    else
      return 0
    fi
  fi

  rm -f "${partial}"
  if command -v curl >/dev/null; then
    curl -fL --retry 5 --retry-delay 2 --connect-timeout 30 \
      -o "${partial}" "${url}" \
      || die "falló la descarga: ${url}"
  elif command -v wget >/dev/null; then
    wget -O "${partial}" "${url}" || die "falló la descarga: ${url}"
  else
    die "hace falta curl o wget para descargar Go/gopls"
  fi

  [[ -f "${partial}" ]] || die "curl/wget no creó ${partial}"
  size="$(wc -c < "${partial}" | tr -d ' ')"
  if [[ "${min_bytes}" -gt 0 && "${size}" -lt "${min_bytes}" ]]; then
    rm -f "${partial}"
    die "descarga incompleta de ${url} (${size} bytes < ${min_bytes})"
  fi
  mv -f "${partial}" "${dest}"
  [[ -f "${dest}" ]] || die "no se pudo mover la descarga a ${dest}"
  printf 'prepare_gopls_bundle: descargado %s (%s bytes)\n' "${dest}" "${size}"
}

require_var TUIDE_GOPLS_VERSION
require_var TUIDE_GOPLS_STAGING_DIR
require_var TUIDE_GOPLS_PAYLOAD_DIR
require_var TUIDE_GOPLS_TAR_PATH
require_var TUIDE_GOPLS_ZST_PATH
require_var TUIDE_GOPLS_MANIFEST_HPP
require_var TUIDE_GOPLS_BLOB_OBJ
require_var TUIDE_BUNDLED_CACHE_DIR
require_var TUIDE_GO_VERSION
require_var TUIDE_GO_URL
require_var TUIDE_GO_TAR_PATH

TUIDE_GOPLS_VERSION="$(strip_cmake_quotes "${TUIDE_GOPLS_VERSION}")"
TUIDE_GOPLS_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_GOPLS_STAGING_DIR}")"
TUIDE_GOPLS_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_GOPLS_PAYLOAD_DIR}")"
TUIDE_GOPLS_TAR_PATH="$(strip_cmake_quotes "${TUIDE_GOPLS_TAR_PATH}")"
TUIDE_GOPLS_ZST_PATH="$(strip_cmake_quotes "${TUIDE_GOPLS_ZST_PATH}")"
TUIDE_GOPLS_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_GOPLS_MANIFEST_HPP}")"
TUIDE_GOPLS_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_GOPLS_BLOB_OBJ}")"
TUIDE_BUNDLED_CACHE_DIR="$(strip_cmake_quotes "${TUIDE_BUNDLED_CACHE_DIR}")"
TUIDE_GO_VERSION="$(strip_cmake_quotes "${TUIDE_GO_VERSION}")"
TUIDE_GO_URL="$(strip_cmake_quotes "${TUIDE_GO_URL}")"
TUIDE_GO_TAR_PATH="$(strip_cmake_quotes "${TUIDE_GO_TAR_PATH}")"

for tool in zstd sha256sum objcopy tar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

rm -rf "${TUIDE_GOPLS_STAGING_DIR}" "${TUIDE_GOPLS_PAYLOAD_DIR}"
mkdir -p "${TUIDE_GOPLS_STAGING_DIR}/bin" "${TUIDE_GOPLS_PAYLOAD_DIR}/bin" \
  "${TUIDE_BUNDLED_CACHE_DIR}"

GO_BIN=""
if command -v go >/dev/null 2>&1; then
  GO_BIN="$(command -v go)"
  printf 'prepare_gopls_bundle: usando Go del sistema: %s (%s)\n' \
    "${GO_BIN}" "$("${GO_BIN}" version 2>/dev/null || echo '?')"
else
  # ~75 MB; rechazar restos truncados de descargas interrumpidas.
  local_min_go_bytes=50000000
  printf 'prepare_gopls_bundle: Go no está en PATH; descargando toolchain %s...\n' \
    "${TUIDE_GO_VERSION}"
  printf 'prepare_gopls_bundle: URL=%s\n' "${TUIDE_GO_URL}"
  printf 'prepare_gopls_bundle: dest=%s\n' "${TUIDE_GO_TAR_PATH}"
  download_file "${TUIDE_GO_URL}" "${TUIDE_GO_TAR_PATH}" "${local_min_go_bytes}"
  [[ -f "${TUIDE_GO_TAR_PATH}" ]] || die "falta el tarball tras descargar: ${TUIDE_GO_TAR_PATH}"

  local_go_root="${TUIDE_GOPLS_STAGING_DIR}/go_toolchain"
  mkdir -p "${local_go_root}"
  tar -xzf "${TUIDE_GO_TAR_PATH}" -C "${local_go_root}" \
    || die "no se pudo extraer ${TUIDE_GO_TAR_PATH} (¿caché corrupta? bórrala y reintenta)"
  # El tarball oficial crea go/ en la raíz.
  if [[ -x "${local_go_root}/go/bin/go" ]]; then
    GO_BIN="${local_go_root}/go/bin/go"
  else
    die "no se encontró go tras extraer ${TUIDE_GO_TAR_PATH}"
  fi
  export GOROOT="${local_go_root}/go"
  printf 'prepare_gopls_bundle: toolchain temporal: %s\n' "$("${GO_BIN}" version)"
fi

export GOBIN="${TUIDE_GOPLS_STAGING_DIR}/bin"
export GO111MODULE=on
export GOTOOLCHAIN="${GOTOOLCHAIN:-auto}"
# Aislar el módulo cache del host.
export GOPATH="${TUIDE_GOPLS_STAGING_DIR}/gopath"
export GOCACHE="${TUIDE_GOPLS_STAGING_DIR}/gocache"
mkdir -p "${GOPATH}" "${GOCACHE}"

"${GO_BIN}" install "golang.org/x/tools/gopls@${TUIDE_GOPLS_VERSION}"

[[ -x "${GOBIN}/gopls" ]] || die "go install no produjo gopls en ${GOBIN}"
cp -a "${GOBIN}/gopls" "${TUIDE_GOPLS_PAYLOAD_DIR}/bin/gopls"
chmod +x "${TUIDE_GOPLS_PAYLOAD_DIR}/bin/gopls"

# Blob interno de gopls (no confundir con el tarball de Go en caché).
rm -f "${TUIDE_GOPLS_TAR_PATH}"
tar -cf "${TUIDE_GOPLS_TAR_PATH}" -C "${TUIDE_GOPLS_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_GOPLS_TAR_PATH}" -o "${TUIDE_GOPLS_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_GOPLS_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_GOPLS_PAYLOAD_DIR}/bin/gopls" | awk '{print $1}')"

cat > "${TUIDE_GOPLS_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_GOPLS_VERSION "${TUIDE_GOPLS_VERSION}"
#define TUIDE_BUNDLED_GOPLS_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_GOPLS_BINARY_SHA256 "${bin_sha}"
EOF

blob_dir="$(dirname "${TUIDE_GOPLS_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_GOPLS_ZST_PATH}")" "$(basename "${TUIDE_GOPLS_BLOB_OBJ}")"
)

printf 'gopls bundle listo: %s (%s bytes)\n' \
  "${TUIDE_GOPLS_ZST_PATH}" "$(wc -c < "${TUIDE_GOPLS_ZST_PATH}")"
