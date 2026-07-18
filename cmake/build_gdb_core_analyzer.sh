#!/usr/bin/env bash
# Builds GDB with Core Analyzer patches and produces a tarball for bundling.
set -euo pipefail

die() {
  printf 'build_gdb_core_analyzer: error: %s\n' "$*" >&2
  exit 1
}

GDB_VERSION="${TUIDE_GDB_CA_GDB_VERSION:-16.3}"
CORE_ANALYZER_REF="${TUIDE_CORE_ANALYZER_REF:-master}"
OUTPUT_TAR_GZ="${1:-}"
BUILD_ROOT="${TUIDE_GDB_CA_BUILD_ROOT:-$(pwd)/third_party/bundled/gdb-ca-build}"
INSTALL_PREFIX="${BUILD_ROOT}/install"
CORE_ANALYZER_DIR="${BUILD_ROOT}/core_analyzer"
GDB_SRC_DIR="${BUILD_ROOT}/gdb-${GDB_VERSION}"

if [[ -z "${OUTPUT_TAR_GZ}" ]]; then
  die "uso: build_gdb_core_analyzer.sh <output.tar.gz>"
fi

ensure_build_deps() {
  local missing=()
  if ! echo '#include <gmp.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libgmp-dev")
  fi
  if ! echo '#include <mpfr.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libmpfr-dev")
  fi
  if ! echo '#include <mpc.h>' | gcc -E - >/dev/null 2>&1; then
    missing+=("libmpc-dev")
  fi
  if [[ ${#missing[@]} -gt 0 ]]; then
    die "faltan dependencias para compilar GDB: ${missing[*]}. Instala con: sudo apt install ${missing[*]}"
  fi
}

ensure_build_deps

mkdir -p "${BUILD_ROOT}"

if [[ ! -d "${CORE_ANALYZER_DIR}/.git" ]]; then
  rm -rf "${CORE_ANALYZER_DIR}"
  git clone --depth 1 --branch "${CORE_ANALYZER_REF}" \
    https://github.com/yanqi27/core_analyzer.git "${CORE_ANALYZER_DIR}"
fi

gdb_tar="${BUILD_ROOT}/gdb-${GDB_VERSION}.tar.gz"
if [[ ! -f "${gdb_tar}" ]]; then
  printf 'descargando GDB %s...\n' "${GDB_VERSION}"
  curl -fL --retry 3 -o "${gdb_tar}" \
    "https://ftp.gnu.org/gnu/gdb/gdb-${GDB_VERSION}.tar.gz"
fi

if [[ ! -d "${GDB_SRC_DIR}" ]]; then
  tar -xf "${gdb_tar}" -C "${BUILD_ROOT}"
fi

patch_dir="${CORE_ANALYZER_DIR}/gdbplus/gdb-${GDB_VERSION}/gdb"
[[ -d "${patch_dir}" ]] || die "no hay parches core_analyzer para gdb-${GDB_VERSION}"

printf 'aplicando parches core_analyzer...\n'
cp -rL "${patch_dir}/." "${GDB_SRC_DIR}/gdb/"
if [[ -d "${CORE_ANALYZER_DIR}/gdbplus/python" ]]; then
  cp -rL "${CORE_ANALYZER_DIR}/gdbplus/python/." "${GDB_SRC_DIR}/gdb/python/"
fi

dap_corefile_dir="$(dirname "$0")/patches/gdb-dap"
if [[ -f "${dap_corefile_dir}/launch.py" && -f "${dap_corefile_dir}/events.py" ]]; then
  printf 'aplicando parche DAP core-file...\n'
  cp "${dap_corefile_dir}/launch.py" "${GDB_SRC_DIR}/gdb/python/lib/gdb/dap/launch.py"
  cp "${dap_corefile_dir}/events.py" "${GDB_SRC_DIR}/gdb/python/lib/gdb/dap/events.py"
fi

gdb_build="${GDB_SRC_DIR}/build"
rm -rf "${gdb_build}"
mkdir -p "${gdb_build}"
cd "${gdb_build}"

gcc_major="$(gcc -dumpversion | cut -f1 -d.)"
conf_extra=()
if [[ "${gcc_major}" -ge 15 ]]; then
  conf_extra+=(CFLAGS=-std=gnu17)
fi

../configure \
  --prefix="${INSTALL_PREFIX}" \
  --with-python=python3 \
  --disable-binutils \
  --disable-ld \
  --disable-gold \
  --disable-gas \
  --disable-sim \
  --disable-gprof \
  "${conf_extra[@]}"

make -j"$(nproc 2>/dev/null || echo 4)"
make install

gdb_bin="${INSTALL_PREFIX}/bin/gdb"
[[ -x "${gdb_bin}" ]] || die "gdb no se instaló en ${gdb_bin}"

if ! "${gdb_bin}" --quiet -i=dap -ex quit >/dev/null 2>&1; then
  die "gdb compilado no soporta DAP"
fi

if ! "${gdb_bin}" --quiet -ex "help obj" -ex quit 2>&1 | grep -qi obj; then
  die "gdb compilado no incluye comandos core analyzer (obj)"
fi

staging="${BUILD_ROOT}/payload"
rm -rf "${staging}"
mkdir -p "${staging}/bin"
cp "${gdb_bin}" "${staging}/bin/gdb"
chmod +x "${staging}/bin/gdb"

rm -f "${OUTPUT_TAR_GZ}"
tar -czf "${OUTPUT_TAR_GZ}" -C "${staging}" .
printf 'gdb+core_analyzer empaquetado: %s\n' "${OUTPUT_TAR_GZ}"
