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

require_var TGDB_CLANGD_VERSION
require_var TGDB_CLANGD_ZIP_PATH
require_var TGDB_CLANGD_ZIP_URL
require_var TGDB_CLANGD_STAGING_DIR
require_var TGDB_CLANGD_PAYLOAD_DIR
require_var TGDB_CLANGD_TAR_PATH
require_var TGDB_CLANGD_ZST_PATH
require_var TGDB_CLANGD_MANIFEST_PATH
require_var TGDB_CLANGD_MANIFEST_HPP
require_var TGDB_CLANGD_BLOB_OBJ

TGDB_CLANGD_ZIP_URL="$(strip_cmake_quotes "${TGDB_CLANGD_ZIP_URL}")"
TGDB_CLANGD_ZIP_PATH="$(strip_cmake_quotes "${TGDB_CLANGD_ZIP_PATH}")"
TGDB_CLANGD_STAGING_DIR="$(strip_cmake_quotes "${TGDB_CLANGD_STAGING_DIR}")"
TGDB_CLANGD_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_CLANGD_PAYLOAD_DIR}")"
TGDB_CLANGD_TAR_PATH="$(strip_cmake_quotes "${TGDB_CLANGD_TAR_PATH}")"
TGDB_CLANGD_ZST_PATH="$(strip_cmake_quotes "${TGDB_CLANGD_ZST_PATH}")"
TGDB_CLANGD_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_CLANGD_MANIFEST_PATH}")"
TGDB_CLANGD_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_CLANGD_MANIFEST_HPP}")"
TGDB_CLANGD_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_CLANGD_BLOB_OBJ}")"
TGDB_CLANGD_VERSION="$(strip_cmake_quotes "${TGDB_CLANGD_VERSION}")"

for tool in unzip strip ldd zstd sha256sum objcopy; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    die "herramienta requerida no encontrada en PATH: ${tool}"
  fi
done

allowed_re='^(linux-vdso\.so|ld-linux-x86-64\.so|libc\.so|libm\.so|libpthread\.so|libdl\.so|librt\.so|libresolv\.so|libgcc_s\.so)'

if [[ ! -f "${TGDB_CLANGD_ZIP_PATH}" ]]; then
  printf 'descargando %s...\n' "${TGDB_CLANGD_ZIP_URL}"
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 -o "${TGDB_CLANGD_ZIP_PATH}" "${TGDB_CLANGD_ZIP_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${TGDB_CLANGD_ZIP_PATH}" "${TGDB_CLANGD_ZIP_URL}"
  else
    die "curl o wget requerido para descargar clangd"
  fi
fi

rm -rf "${TGDB_CLANGD_STAGING_DIR}"
mkdir -p "${TGDB_CLANGD_STAGING_DIR}"
unzip -q "${TGDB_CLANGD_ZIP_PATH}" -d "${TGDB_CLANGD_STAGING_DIR}"

clangd_bin="$(find "${TGDB_CLANGD_STAGING_DIR}" -mindepth 2 -type f -path '*/bin/clangd' | head -n1)"
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

rm -rf "${TGDB_CLANGD_PAYLOAD_DIR}"
mkdir -p "${TGDB_CLANGD_PAYLOAD_DIR}/bin" "${TGDB_CLANGD_PAYLOAD_DIR}/${resource_dir_rel}"
cp "${clangd_bin}" "${TGDB_CLANGD_PAYLOAD_DIR}/bin/clangd"
cp -a "${clangd_root}/${resource_dir_rel}/." "${TGDB_CLANGD_PAYLOAD_DIR}/${resource_dir_rel}/"

rm -f "${TGDB_CLANGD_TAR_PATH}"
tar -cf "${TGDB_CLANGD_TAR_PATH}" -C "${TGDB_CLANGD_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_CLANGD_TAR_PATH}" -o "${TGDB_CLANGD_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_CLANGD_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_CLANGD_PAYLOAD_DIR}/bin/clangd" | awk '{print $1}')"

cat > "${TGDB_CLANGD_MANIFEST_PATH}" <<EOF
{
  "version": "${TGDB_CLANGD_VERSION}",
  "blob_sha256": "${blob_sha}",
  "binary_sha256": "${bin_sha}",
  "resource_subdir": "${resource_dir_rel}"
}
EOF

cat > "${TGDB_CLANGD_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_CLANGD_VERSION "${TGDB_CLANGD_VERSION}"
#define TGDB_BUNDLED_CLANGD_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_CLANGD_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_CLANGD_RESOURCE_SUBDIR "${resource_dir_rel}"
EOF

blob_dir="$(dirname "${TGDB_CLANGD_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_CLANGD_ZST_PATH}")" "$(basename "${TGDB_CLANGD_BLOB_OBJ}")"
)

printf 'clangd bundle listo: %s (%s bytes)\n' "${TGDB_CLANGD_ZST_PATH}" "$(wc -c < "${TGDB_CLANGD_ZST_PATH}")"
