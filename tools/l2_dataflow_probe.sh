#!/usr/bin/env bash
# Data-flow probe (rg-only, no LSP/LLM).
# Uso: ./tools/l2_dataflow_probe.sh [VAR…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build/l2_harness_cli"
if [[ ! -x "$BIN" ]]; then
  echo "building l2_harness_cli…"
  cmake --build "${ROOT}/build" --target l2_harness_cli -j"$(nproc)"
fi

VARS=("$@")
if [[ ${#VARS[@]} -eq 0 ]]; then
  echo "Uso: $0 VAR [VAR…]" >&2
  exit 2
fi

export TUIDE_ROOT="$ROOT"
cd "$ROOT"

echo "==== l2 dataflow-probe (${#VARS[@]} vars) ===="
fail=0
for var in "${VARS[@]}"; do
  echo
  args=(dataflow-probe "$var")
  if ! "$BIN" "${args[@]}"; then
    fail=$((fail + 1))
  fi
done

echo
if [[ "$fail" -gt 0 ]]; then
  echo "DONE with $fail var(s) empty"
  exit 1
fi
echo "DONE ok"
exit 0
