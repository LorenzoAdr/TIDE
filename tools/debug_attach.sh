#!/usr/bin/env bash
# Depura tgdb bajo GDB y captura el stack en el punto exacto del throw.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/build"
TGDB="${BUILD}/tgdb"
HELLO="${BUILD}/hello"

if [[ ! -x "${TGDB}" ]]; then
  echo "Compila primero: cmake --build ${BUILD}" >&2
  exit 1
fi

if [[ ! -x "${HELLO}" ]]; then
  cmake --build "${BUILD}" --target hello -j"$(nproc 2>/dev/null || echo 4)"
fi

# Mata restos de hello anteriores.
pkill -f "${HELLO}" 2>/dev/null || true
sleep 0.2

"${HELLO}" >/dev/null 2>&1 &
HELLO_PID=$!
cleanup() {
  kill "${HELLO_PID}" 2>/dev/null || true
}
trap cleanup EXIT

sleep 0.3
echo "hello PID=${HELLO_PID}"
echo
echo "GDB capturará std::bad_function_call en el throw (no en main)."
echo "Para reproducir el flujo del wizard: arranca sin --attach y usa F2 manualmente."
echo

exec gdb -iex "set pagination off" \
  -ex "catch throw" \
  -ex "run" \
  -ex "bt 40" \
  -ex "frame 4" \
  -ex "list" \
  --args "${TGDB}" --cwd "${ROOT}" --attach "${HELLO_PID}" "${HELLO}"
