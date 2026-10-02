#!/usr/bin/env bash
# F1 anchor-hunt battery: stops at f1_done / anchor_miss (no pack B).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-f1_anchor_smoke}"
CASE_FILTER="${2:-}"
ONLY="${3:-}"

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL=1
export TUIDE_ROOT="$ROOT"

ROUND=".tuide/ai/l2_explore_battery/round_${LABEL}"
mkdir -p "$ROUND"

CASES="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
mapfile -t IDS < <(python3 - <<'PY' "$CASES" "$CASE_FILTER"
import json, sys
cases = json.load(open(sys.argv[1]))
filt = sys.argv[2].strip()
for c in cases:
    cid = c["id"]
    if filt and filt not in cid:
        continue
    print(cid)
PY
)

if [[ -n "$ONLY" && "$ONLY" != "only" ]]; then
  IDS=("$ONLY")
fi

for id in "${IDS[@]}"; do
  echo "=== F1 $id ==="
  OUT="$ROUND/$id"
  mkdir -p "$OUT"
  ./build/tools/l2_harness_cli run-explore-a \
    --workspace "$ROOT" \
    --case-id "$id" \
    --output "$OUT" \
    --stop-at-phase-a \
    2>&1 | tee "$OUT/run.log" || true
done

python3 "$ROOT/tools/l2_explore_battery/score_f1_anchor.py" \
  --round-dir "$ROUND" --cases "$CASES"
