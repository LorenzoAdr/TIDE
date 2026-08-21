#!/usr/bin/env bash
# One-shot: trail judge → suspect vars → dataflow-probe (rg).
# Uso: ./tools/l2_trail_suspect_shot.sh [--dry] [extra…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="$ROOT/build/l2_harness_cli"
OUT="$ROOT/.tuide/ai/l2_explore_battery/trail_suspect_shot_v1"
mkdir -p "$OUT"

if [[ ! -x "$BIN" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

export TUIDE_ROOT="$ROOT"
export L2_FEAT_L2_EXPLORE_PHASE_A=1

python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

ARGS=(
  trail-judge-shot set_busy_spinner
  --path src/ui/busy_strip.cpp
  --case 17_ai_spinner_stuck
  --gold begin_thinking
  --gold-var agent_busy_
  --suspect
  --out "$OUT"
)

echo "==== trail-suspect-shot → $OUT ===="
set +e
"$BIN" "${ARGS[@]}" "$@"
RC=$?
set -e
echo "exit=$RC artifacts=$OUT"
ls -la "$OUT" 2>/dev/null | head -20 || true
exit "$RC"
