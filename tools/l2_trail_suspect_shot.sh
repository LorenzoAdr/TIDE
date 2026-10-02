#!/usr/bin/env bash
# One-shot: generic trail judge → suspect vars → dataflow-probe.
# Uso: ./tools/l2_trail_suspect_shot.sh SYM --path PATH (--case ID|--instruction TEXT) [args…]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="$ROOT/build/l2_harness_cli"
if [[ ! -x "$BIN" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

export TUIDE_ROOT="$ROOT"
export L2_FEAT_L2_EXPLORE_PHASE_A=1

python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

exec "$BIN" trail-judge-shot --suspect "$@"
