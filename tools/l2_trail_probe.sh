#!/usr/bin/env bash
# Quick trail/call-hierarchy battery — no LLM.
# Usage: ./tools/l2_trail_probe.sh [SYM…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build/l2_harness_cli"
if [[ ! -x "$BIN" ]]; then
  echo "building l2_harness_cli…"
  cmake --build "${ROOT}/build" --target l2_harness_cli -j"$(nproc)"
fi

SYMS=("$@")
if [[ ${#SYMS[@]} -eq 0 ]]; then
  echo "Uso: $0 SYM [SYM…]" >&2
  exit 2
fi

export TUIDE_ROOT="$ROOT"
cd "$ROOT"

echo "==== l2 trail-probe battery (${#SYMS[@]} symbols) ===="
fail=0
for sym in "${SYMS[@]}"; do
  echo
  args=(trail-probe "$sym")
  if ! "$BIN" "${args[@]}"; then
    fail=$((fail + 1))
  fi
done

echo
if [[ "$fail" -gt 0 ]]; then
  echo "DONE with $fail symbol(s) empty"
  exit 1
fi
echo "DONE ok"
exit 0
