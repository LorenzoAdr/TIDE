#!/usr/bin/env bash
# Overnight zone-judge: epistemic + legacy ablation, scoring, MORNING.md
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
chmod +x tools/l2_explore_battery/run_overnight_zone_judge.py 2>/dev/null || true
exec python3 "$ROOT/tools/l2_explore_battery/run_overnight_zone_judge.py" "$@"
