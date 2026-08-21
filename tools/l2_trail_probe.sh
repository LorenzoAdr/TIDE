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
  # Caso 17 / spinner: control UI + callers AI + distractor LSP
  SYMS=(
    set_busy_spinner
    begin_thinking
    end_thinking
    agent_busy
    clear_busy_if
    cancel_inflight_completion
  )
fi

export TUIDE_ROOT="$ROOT"
cd "$ROOT"

path_hint_for() {
  case "$1" in
    set_busy_spinner|clear_busy_if) echo "src/ui/busy_strip.cpp" ;;
    begin_thinking|end_thinking|agent_busy) echo "src/ai/ai_controller.cpp" ;;
    cancel_inflight_completion) echo "src/lsp/lsp_client.cpp" ;;
    *) echo "" ;;
  esac
}

echo "==== l2 trail-probe battery (${#SYMS[@]} symbols) ===="
fail=0
for sym in "${SYMS[@]}"; do
  echo
  hint="$(path_hint_for "$sym")"
  args=(trail-probe "$sym")
  if [[ -n "$hint" ]]; then
    args+=(--path "$hint")
  fi
  if ! "$BIN" "${args[@]}"; then
    fail=$((fail + 1))
  fi
done

echo
echo "==== persist round-trip (set_busy_spinner) ===="
"$BIN" trail-probe set_busy_spinner --path src/ui/busy_strip.cpp --persist | tail -n 8

echo
if [[ "$fail" -gt 0 ]]; then
  echo "DONE with $fail symbol(s) empty"
  exit 1
fi
echo "DONE ok"
exit 0
