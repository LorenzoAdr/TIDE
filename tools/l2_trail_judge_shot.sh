#!/usr/bin/env bash
# One-shot LLM: inject trail of map L0 (set_busy_spinner) with nested AI control,
# ask a_trail_judge — does the model mark begin_thinking interesting?
# Uso: ./tools/l2_trail_judge_shot.sh [--dry] [extra args…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="$ROOT/build/l2_harness_cli"
OUT="$ROOT/.tuide/ai/l2_explore_battery/trail_judge_shot_v1"
mkdir -p "$OUT"

if [[ ! -x "$BIN" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

export TUIDE_ROOT="$ROOT"
export L2_FEAT_L2_EXPLORE_PHASE_A=1

# Kill leftover local servers if any (same ports as battery)
python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

ARGS=(
  trail-judge-shot set_busy_spinner
  --path src/ui/busy_strip.cpp
  --case 17_ai_spinner_stuck
  --gold begin_thinking
  --out "$OUT"
)

echo "==== trail-judge-shot → $OUT ===="
set +e
"$BIN" "${ARGS[@]}" "$@"
RC=$?
set -e
echo "exit=$RC artifacts=$OUT"
exit "$RC"
