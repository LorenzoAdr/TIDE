#!/usr/bin/env bash
# Thin wrapper around the autonomous Python sweep daemon.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
chmod +x tools/l2_facet_sweep.py tools/l2_battery/score_facets.py tools/l2_battery/run_closed_loop_round.py 2>/dev/null || true
exec python3 "$ROOT/tools/l2_facet_sweep.py" "$@"
