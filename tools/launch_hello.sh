#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT}/build"
HELLO="${BUILD_DIR}/hello"

if [[ ! -x "${HELLO}" ]]; then
  cmake -S "${ROOT}" -B "${BUILD_DIR}" >/dev/null
  cmake --build "${BUILD_DIR}" --target hello -j"$(nproc 2>/dev/null || echo 4)"
fi

cat <<EOF
Lanzando proceso de prueba: ${HELLO}
Imprime un contador cada segundo (bucle infinito) para poder hacer attach
y ver variables locales (x, y, sum, acc, counter) mientras avanza.

En otra terminal, adjunta el depurador con:
  ${ROOT}/tools/launch.sh --attach <PID> ${HELLO}

Alternativa sin attach (recomendada si ptrace falla):
  ${ROOT}/tools/launch.sh ${HELLO}

Nota Linux: Ubuntu suele tener kernel.yama.ptrace_scope=1, que bloquea attach
a procesos lanzados aparte. hello llama a prctl(PR_SET_PTRACER) para permitirlo.
Si gdb manual sigue fallando: sudo sysctl kernel.yama.ptrace_scope=0

EOF

exec "${HELLO}"
