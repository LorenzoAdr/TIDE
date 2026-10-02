#!/usr/bin/env bash
# Batería NL cortada en registry-query --trails (sin LLM de localización).
# Uso:
#   ./tools/l2_registry_trails_battery_run.sh [LABEL] [START_AT] [only]
# Ejemplos:
#   ./tools/l2_registry_trails_battery_run.sh registry_trails_v1
#   ./tools/l2_registry_trails_battery_run.sh smoke_trails CASE_ID only
#   ./tools/l2_registry_trails_battery_run.sh registry_trails_v2 --skip-l1 --skip-ingest --skip-embed \
#       --maps-from .tuide/ai/l2_explore_battery/round_registry_trails_v1
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-registry_trails_v1}"
START_AT="${2:-}"
ONLY="${3:-}"
ARGS=(--label "$LABEL")
if [[ -n "$START_AT" ]]; then
  ARGS+=(--start-at "$START_AT")
fi
if [[ "${ONLY,,}" == "only" || "${ONLY}" == "1" || "${ONLY,,}" == "yes" ]]; then
  ARGS+=(--only)
fi
# Extra flags after the 3 positional args: pass through (e.g. --skip-embed).
shift $(( $# < 3 ? $# : 3 )) || true
ARGS+=("$@")
export PYTHONUNBUFFERED=1
export TUIDE_ROOT="$ROOT"
exec python3 -u "$ROOT/tools/l2_registry_trails_battery.py" "${ARGS[@]}"
