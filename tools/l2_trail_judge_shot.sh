#!/usr/bin/env bash
# One-shot LLM over a caller-supplied trail focus.
# Uso: ./tools/l2_trail_judge_shot.sh SYM --path PATH (--case ID|--instruction TEXT) [args…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="$ROOT/build/l2_harness_cli"
if [[ ! -x "$BIN" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

export TUIDE_ROOT="$ROOT"
export L2_FEAT_L2_EXPLORE_PHASE_A=1

# Kill leftover local servers if any (same ports as battery)
python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

exec "$BIN" trail-judge-shot "$@"
