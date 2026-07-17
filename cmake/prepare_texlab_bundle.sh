#!/usr/bin/env bash
# Empaqueta TexLab + ChkTeX (+ libpcre2) Linux x86_64 para TGDB_BUNDLE_TEXLAB.
set -euo pipefail

die() { printf 'prepare_texlab_bundle: error: %s\n' "$*" >&2; exit 1; }
strip_cmake_quotes() {
  local value="$1"
  value="${value#\"}"
  value="${value%\"}"
  printf '%s' "${value}"
}
require_var() { [[ -n "${!1:-}" ]] || die "variable requerida: $1"; }

require_var TGDB_TEXLAB_VERSION
require_var TGDB_TEXLAB_URL
require_var TGDB_TEXLAB_TAR_PATH
require_var TGDB_TEXLAB_STAGING_DIR
require_var TGDB_TEXLAB_PAYLOAD_DIR
require_var TGDB_TEXLAB_TAR_PATH_OUT
require_var TGDB_TEXLAB_ZST_PATH
require_var TGDB_TEXLAB_MANIFEST_PATH
require_var TGDB_TEXLAB_MANIFEST_HPP
require_var TGDB_TEXLAB_BLOB_OBJ

TGDB_TEXLAB_VERSION="$(strip_cmake_quotes "${TGDB_TEXLAB_VERSION}")"
TGDB_TEXLAB_URL="$(strip_cmake_quotes "${TGDB_TEXLAB_URL}")"
TGDB_TEXLAB_TAR_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_TAR_PATH}")"
TGDB_TEXLAB_STAGING_DIR="$(strip_cmake_quotes "${TGDB_TEXLAB_STAGING_DIR}")"
TGDB_TEXLAB_PAYLOAD_DIR="$(strip_cmake_quotes "${TGDB_TEXLAB_PAYLOAD_DIR}")"
TGDB_TEXLAB_TAR_PATH_OUT="$(strip_cmake_quotes "${TGDB_TEXLAB_TAR_PATH_OUT}")"
TGDB_TEXLAB_ZST_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_ZST_PATH}")"
TGDB_TEXLAB_MANIFEST_PATH="$(strip_cmake_quotes "${TGDB_TEXLAB_MANIFEST_PATH}")"
TGDB_TEXLAB_MANIFEST_HPP="$(strip_cmake_quotes "${TGDB_TEXLAB_MANIFEST_HPP}")"
TGDB_TEXLAB_BLOB_OBJ="$(strip_cmake_quotes "${TGDB_TEXLAB_BLOB_OBJ}")"

for tool in zstd sha256sum objcopy tar ar; do
  command -v "${tool}" >/dev/null || die "falta ${tool}"
done

download_file() {
  local url="$1"
  local dest="$2"
  mkdir -p "$(dirname "${dest}")"
  if command -v curl >/dev/null; then
    curl -fL --retry 3 -o "${dest}" "${url}"
  else
    wget -O "${dest}" "${url}"
  fi
}

extract_deb_to() {
  local deb_path="$1"
  local dest_dir="$2"
  mkdir -p "${dest_dir}"
  (
    cd "${dest_dir}"
    ar x "${deb_path}"
    local data_tar
    data_tar="$(ls data.tar.* 2>/dev/null | head -n1)"
    [[ -n "${data_tar}" ]] || die "no data.tar.* en $(basename "${deb_path}")"
    tar -xf "${data_tar}"
  )
}

install_shared_libs_from_deb() {
  local extract_dir="$1"
  local lib_dest="$2"
  mkdir -p "${lib_dest}"
  local so
  for so in "${extract_dir}"/usr/lib/*/libpcre2*.so*; do
    [[ -e "${so}" ]] || continue
    cp -a "${so}" "${lib_dest}/$(basename "${so}")"
  done
}

if [[ ! -f "${TGDB_TEXLAB_TAR_PATH}" ]]; then
  download_file "${TGDB_TEXLAB_URL}" "${TGDB_TEXLAB_TAR_PATH}"
fi

# ChkTeX (lint companion; same role as shellcheck in the bash-ls blob).
# Strip CMake quotes from every env override (required when values contain special chars).
TGDB_CHKTEX_VERSION="$(strip_cmake_quotes "${TGDB_CHKTEX_VERSION:-1.7.10-1}")"
TGDB_CHKTEX_DEB_NAME="$(strip_cmake_quotes "${TGDB_CHKTEX_DEB_NAME:-chktex_${TGDB_CHKTEX_VERSION}_amd64.deb}")"
TGDB_CHKTEX_DEB_URL="$(strip_cmake_quotes "${TGDB_CHKTEX_DEB_URL:-https://deb.debian.org/debian/pool/main/c/chktex/${TGDB_CHKTEX_DEB_NAME}}")"
TGDB_CHKTEX_DEB_PATH="$(strip_cmake_quotes "${TGDB_CHKTEX_DEB_PATH:-$(dirname "${TGDB_TEXLAB_TAR_PATH}")/${TGDB_CHKTEX_DEB_NAME}}")"
# Runtime libs for the Debian chktex binary (host may lack libpcre2-posix.so.3).
# Prefer a version without '~' so CMake/make do not re-quote KEY=VALUE for the shell.
TGDB_PCRE2_VERSION="$(strip_cmake_quotes "${TGDB_PCRE2_VERSION:-10.47-2}")"
TGDB_PCRE2_POSIX_DEB_NAME="$(strip_cmake_quotes "${TGDB_PCRE2_POSIX_DEB_NAME:-libpcre2-posix3_${TGDB_PCRE2_VERSION}_amd64.deb}")"
TGDB_PCRE2_POSIX_DEB_URL="$(strip_cmake_quotes "${TGDB_PCRE2_POSIX_DEB_URL:-https://deb.debian.org/debian/pool/main/p/pcre2/${TGDB_PCRE2_POSIX_DEB_NAME}}")"
TGDB_PCRE2_POSIX_DEB_PATH="$(strip_cmake_quotes "${TGDB_PCRE2_POSIX_DEB_PATH:-$(dirname "${TGDB_TEXLAB_TAR_PATH}")/${TGDB_PCRE2_POSIX_DEB_NAME}}")"
TGDB_PCRE2_8_DEB_NAME="$(strip_cmake_quotes "${TGDB_PCRE2_8_DEB_NAME:-libpcre2-8-0_${TGDB_PCRE2_VERSION}_amd64.deb}")"
TGDB_PCRE2_8_DEB_URL="$(strip_cmake_quotes "${TGDB_PCRE2_8_DEB_URL:-https://deb.debian.org/debian/pool/main/p/pcre2/${TGDB_PCRE2_8_DEB_NAME}}")"
TGDB_PCRE2_8_DEB_PATH="$(strip_cmake_quotes "${TGDB_PCRE2_8_DEB_PATH:-$(dirname "${TGDB_TEXLAB_TAR_PATH}")/${TGDB_PCRE2_8_DEB_NAME}}")"

if [[ ! -f "${TGDB_CHKTEX_DEB_PATH}" ]]; then
  download_file "${TGDB_CHKTEX_DEB_URL}" "${TGDB_CHKTEX_DEB_PATH}"
fi
if [[ ! -f "${TGDB_PCRE2_POSIX_DEB_PATH}" ]]; then
  download_file "${TGDB_PCRE2_POSIX_DEB_URL}" "${TGDB_PCRE2_POSIX_DEB_PATH}"
fi
if [[ ! -f "${TGDB_PCRE2_8_DEB_PATH}" ]]; then
  download_file "${TGDB_PCRE2_8_DEB_URL}" "${TGDB_PCRE2_8_DEB_PATH}"
fi

rm -rf "${TGDB_TEXLAB_STAGING_DIR}" "${TGDB_TEXLAB_PAYLOAD_DIR}"
mkdir -p "${TGDB_TEXLAB_STAGING_DIR}" \
  "${TGDB_TEXLAB_PAYLOAD_DIR}/bin" \
  "${TGDB_TEXLAB_PAYLOAD_DIR}/libexec" \
  "${TGDB_TEXLAB_PAYLOAD_DIR}/lib"

tar -xzf "${TGDB_TEXLAB_TAR_PATH}" -C "${TGDB_TEXLAB_STAGING_DIR}"
texlab_bin="$(find "${TGDB_TEXLAB_STAGING_DIR}" -type f -name texlab | head -n1)"
[[ -n "${texlab_bin}" ]] || die "no se encontró texlab en el tarball"
cp -a "${texlab_bin}" "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab"
chmod +x "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab"

chktex_extract="${TGDB_TEXLAB_STAGING_DIR}/chktex_deb"
extract_deb_to "${TGDB_CHKTEX_DEB_PATH}" "${chktex_extract}"
chktex_bin="$(find "${chktex_extract}" -type f -path '*/usr/bin/chktex' | head -n1)"
[[ -n "${chktex_bin}" && -x "${chktex_bin}" ]] || die "no se encontró chktex en el .deb"
install -m 0755 "${chktex_bin}" "${TGDB_TEXLAB_PAYLOAD_DIR}/libexec/chktex"

posix_extract="${TGDB_TEXLAB_STAGING_DIR}/pcre2_posix_deb"
eight_extract="${TGDB_TEXLAB_STAGING_DIR}/pcre2_8_deb"
extract_deb_to "${TGDB_PCRE2_POSIX_DEB_PATH}" "${posix_extract}"
extract_deb_to "${TGDB_PCRE2_8_DEB_PATH}" "${eight_extract}"
install_shared_libs_from_deb "${posix_extract}" "${TGDB_TEXLAB_PAYLOAD_DIR}/lib"
install_shared_libs_from_deb "${eight_extract}" "${TGDB_TEXLAB_PAYLOAD_DIR}/lib"
[[ -e "${TGDB_TEXLAB_PAYLOAD_DIR}/lib/libpcre2-posix.so.3" ]] \
  || die "falta libpcre2-posix.so.3 en el payload"
[[ -e "${TGDB_TEXLAB_PAYLOAD_DIR}/lib/libpcre2-8.so.0" ]] \
  || die "falta libpcre2-8.so.0 en el payload"

# Wrapper so TexLab's `chktex` on PATH finds bundled libs without polluting the host.
cat > "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/chktex" <<'EOF'
#!/bin/sh
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$ROOT/libexec/chktex" "$@"
EOF
chmod +x "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/chktex"

rm -f "${TGDB_TEXLAB_TAR_PATH_OUT}"
tar -cf "${TGDB_TEXLAB_TAR_PATH_OUT}" -C "${TGDB_TEXLAB_PAYLOAD_DIR}" .
zstd -f -19 -q "${TGDB_TEXLAB_TAR_PATH_OUT}" -o "${TGDB_TEXLAB_ZST_PATH}"

blob_sha="$(sha256sum "${TGDB_TEXLAB_ZST_PATH}" | awk '{print $1}')"
bin_sha="$(sha256sum "${TGDB_TEXLAB_PAYLOAD_DIR}/bin/texlab" | awk '{print $1}')"
chktex_sha="$(sha256sum "${TGDB_TEXLAB_PAYLOAD_DIR}/libexec/chktex" | awk '{print $1}')"

cat > "${TGDB_TEXLAB_MANIFEST_PATH}" <<EOF
{"version":"${TGDB_TEXLAB_VERSION}","blob_sha256":"${blob_sha}","binary_sha256":"${bin_sha}","chktex_sha256":"${chktex_sha}","chktex_version":"${TGDB_CHKTEX_VERSION}","pcre2_version":"${TGDB_PCRE2_VERSION}"}
EOF

cat > "${TGDB_TEXLAB_MANIFEST_HPP}" <<EOF
#pragma once
#define TGDB_BUNDLED_TEXLAB_VERSION "${TGDB_TEXLAB_VERSION}"
#define TGDB_BUNDLED_TEXLAB_BLOB_SHA256 "${blob_sha}"
#define TGDB_BUNDLED_TEXLAB_BINARY_SHA256 "${bin_sha}"
#define TGDB_BUNDLED_CHKTEX_VERSION "${TGDB_CHKTEX_VERSION}"
#define TGDB_BUNDLED_CHKTEX_SHA256 "${chktex_sha}"
#define TGDB_BUNDLED_PCRE2_VERSION "${TGDB_PCRE2_VERSION}"
EOF

blob_dir="$(dirname "${TGDB_TEXLAB_BLOB_OBJ}")"
(
  cd "${blob_dir}"
  objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
    --rename-section .data=.rodata,alloc,load,readonly,data,contents \
    "$(basename "${TGDB_TEXLAB_ZST_PATH}")" "$(basename "${TGDB_TEXLAB_BLOB_OBJ}")"
)

printf 'texlab+chktex bundle listo: %s (%s bytes)\n' \
  "${TGDB_TEXLAB_ZST_PATH}" "$(wc -c < "${TGDB_TEXLAB_ZST_PATH}")"
