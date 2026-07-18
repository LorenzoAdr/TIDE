#!/usr/bin/env bash
set -euo pipefail

die() {
  printf 'prepare_clangd_bundle: error: %s\n' "$*" >&2
  exit 1
}

strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}

require_var() {
  if [[ -z "${!1:-}" ]]; then
    die "variable de entorno requerida: $1"
  fi
}

require_var TUIDE_CLANGD_VERSION
require_var TUIDE_CLANGD_ZIP_PATH
require_var TUIDE_CLANGD_ZIP_URL
require_var TUIDE_CLANGD_STAGING_DIR
require_var TUIDE_CLANGD_PAYLOAD_DIR
require_var TUIDE_CLANGD_TAR_PATH
require_var TUIDE_CLANGD_ZST_PATH
require_var TUIDE_CLANGD_MANIFEST_PATH
require_var TUIDE_CLANGD_MANIFEST_HPP
require_var TUIDE_CLANGD_BLOB_OBJ

TUIDE_CLANGD_ZIP_URL="$(strip_cmake_quotes "${TUIDE_CLANGD_ZIP_URL}")"
TUIDE_CLANGD_ZIP_PATH="$(strip_cmake_quotes "${TUIDE_CLANGD_ZIP_PATH}")"
TUIDE_CLANGD_STAGING_DIR="$(strip_cmake_quotes "${TUIDE_CLANGD_STAGING_DIR}")"
TUIDE_CLANGD_PAYLOAD_DIR="$(strip_cmake_quotes "${TUIDE_CLANGD_PAYLOAD_DIR}")"
TUIDE_CLANGD_TAR_PATH="$(strip_cmake_quotes "${TUIDE_CLANGD_TAR_PATH}")"
TUIDE_CLANGD_ZST_PATH="$(strip_cmake_quotes "${TUIDE_CLANGD_ZST_PATH}")"
TUIDE_CLANGD_MANIFEST_PATH="$(strip_cmake_quotes "${TUIDE_CLANGD_MANIFEST_PATH}")"
TUIDE_CLANGD_MANIFEST_HPP="$(strip_cmake_quotes "${TUIDE_CLANGD_MANIFEST_HPP}")"
TUIDE_CLANGD_BLOB_OBJ="$(strip_cmake_quotes "${TUIDE_CLANGD_BLOB_OBJ}")"
TUIDE_CLANGD_VERSION="$(strip_cmake_quotes "${TUIDE_CLANGD_VERSION}")"

for tool in unzip strip ldd zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

allowed_re='^(linux-vdso\.so|ld-linux-x86-64\.so|libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so|libgcc_s\.so)'

download_clangd_zip() {
  printf 'descargando %s...\n' "${TUIDE_CLANGD_ZIP_URL}"
  local tmp_path="${TUIDE_CLANGD_ZIP_PATH}.partial"
  rm -f "${tmp_path}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${tmp_path}" "${TUIDE_CLANGD_ZIP_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${tmp_path}" "${TUIDE_CLANGD_ZIP_URL}"
  else
    die "curl o wget requerido para descargar clangd"
  fi
  mv -f "${tmp_path}" "${TUIDE_CLANGD_ZIP_PATH}"
}

zip_is_valid() {
  [[ -f "$1" ]] && unzip -tqq "$1" >/dev/null 2>&1
}

if ! zip_is_valid "${TUIDE_CLANGD_ZIP_PATH}"; then
  if [[ -f "${TUIDE_CLANGD_ZIP_PATH}" ]]; then
    printf 'cache zip corrupto o incompleto, re-descargando: %s\n' "${TUIDE_CLANGD_ZIP_PATH}"
    rm -f "${TUIDE_CLANGD_ZIP_PATH}"
  fi
  download_clangd_zip
  zip_is_valid "${TUIDE_CLANGD_ZIP_PATH}" || die "zip de clangd inválido tras descargar: ${TUIDE_CLANGD_ZIP_PATH}"
fi

rm -rf "${TUIDE_CLANGD_STAGING_DIR}"
mkdir -p "${TUIDE_CLANGD_STAGING_DIR}"
unzip -q "${TUIDE_CLANGD_ZIP_PATH}" -d "${TUIDE_CLANGD_STAGING_DIR}"

clangd_bin="$(find "${TUIDE_CLANGD_STAGING_DIR}" -mindepth 2 -type f -path '*/bin/clangd' | head -n1)"
[[ -n "${clangd_bin}" && -f "${clangd_bin}" ]] || die "no se encontró bin/clangd en el release"
chmod +x "${clangd_bin}"
clangd_root="$(dirname "$(dirname "${clangd_bin}")")"

resource_glob=( "${clangd_root}"/lib/clang/*/include )
[[ -d "${resource_glob[0]}" ]] || die "no se encontró lib/clang/*/include"

resource_dir_rel="${resource_glob[0]#${clangd_root}/}"
resource_dir_rel="${resource_dir_rel%/include}"

strip -s "${clangd_bin}"

while IFS= read -r line; do
  lib=""
  if [[ "${line}" =~ =\>\ (.+)\ \( ]]; then
    lib="${BASH_REMATCH[1]}"
  elif [[ "${line}" =~ ^(/[^[:space:]]+) ]]; then
    lib="${BASH_REMATCH[1]}"
  else
    continue
  fi
  base="$(basename "${lib}")"
  if [[ ! "${base}" =~ ${allowed_re} ]]; then
    die "dependencia dinámica no permitida en clangd: ${lib}"
  fi
done < <(ldd "${clangd_bin}")

rm -rf "${TUIDE_CLANGD_PAYLOAD_DIR}"
mkdir -p "${TUIDE_CLANGD_PAYLOAD_DIR}/bin" "${TUIDE_CLANGD_PAYLOAD_DIR}/${resource_dir_rel}"
cp "${clangd_bin}" "${TUIDE_CLANGD_PAYLOAD_DIR}/bin/clangd"
cp -a "${clangd_root}/${resource_dir_rel}/." "${TUIDE_CLANGD_PAYLOAD_DIR}/${resource_dir_rel}/"

rm -f "${TUIDE_CLANGD_TAR_PATH}"
tar -cf "${TUIDE_CLANGD_TAR_PATH}" -C "${TUIDE_CLANGD_PAYLOAD_DIR}" .
zstd -f -19 -q "${TUIDE_CLANGD_TAR_PATH}" -o "${TUIDE_CLANGD_ZST_PATH}"

blob_sha="$(sha256sum "${TUIDE_CLANGD_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TUIDE_CLANGD_PAYLOAD_DIR}/bin/clangd" | awk '{print $1}')"

cat > "${TUIDE_CLANGD_MANIFEST_PATH}" <<EOF
{
  "version": "${TUIDE_CLANGD_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "resource_subdir": "${resource_dir_rel}"
}
EOF

cat > "${TUIDE_CLANGD_MANIFEST_HPP}" <<EOF
#pragma once
#define TUIDE_BUNDLED_CLANGD_VERSION "${TUIDE_CLANGD_VERSION}"
#define TUIDE_BUNDLED_CLANGD_BLOB_SHA256 "${blob_sha}"
#define TUIDE_BUNDLED_CLANGD_BINARY_SHA256 "${bin_sha}"
#define TUIDE_BUNDLED_CLANGD_RESOURCE_SUBDIR "${resource_dir_rel}"
EOF

blob_dir="$(dirname "${TUIDE_CLANGD_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TUIDE_CLANGD_ZST_PATH}")" "$(basename "${TUIDE_CLANGD_BLOB_OBJ}")"
)

printf 'clangd bundle listo: %s (%s bytes)\n' "${TUIDE_CLANGD_ZST_PATH}" "$(wc -c < "${TUIDE_CLANGD_ZST_PATH}")"
