#!/usr/bin/env bash
# Methodical core5 plan: L1 → graph → F1 with gate checks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PHASE="${1:-all}"
LABEL="${2:-core5_v1}"
CASES="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human_core5.json"
ONLY="${3:-}"
EXTRA=()
if [[ -n "$ONLY" ]]; then
  EXTRA=(--only "$ONLY")
fi

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export TUIDE_ROOT="$ROOT"

chmod +x "$ROOT/tools/l2_core5_battery.py"

case "$PHASE" in
  probe)
    python3 "$ROOT/tools/l2_core5_battery.py" probe
    ;;
  l1)
    python3 "$ROOT/tools/l2_core5_battery.py" l1 --label "${LABEL}_l1" --cases "$CASES" \
      "${EXTRA[@]}" --check-gate
    ;;
  graph)
    FROM="${4:-$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}_l1}"
    python3 "$ROOT/tools/l2_core5_battery.py" graph --label "${LABEL}_graph" --cases "$CASES" \
      --from-round "$FROM" "${EXTRA[@]}" --check-gate
    ;;
  f1)
    FROM="${4:-$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}_l1}"
    export L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL=1
    python3 "$ROOT/tools/l2_core5_battery.py" f1 --label "${LABEL}_f1" --cases "$CASES" \
      --from-round "$FROM" "${EXTRA[@]}" --check-gate
    ;;
  all)
    export L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL=1
    python3 "$ROOT/tools/l2_core5_battery.py" all --label "$LABEL" --cases "$CASES" "${EXTRA[@]}"
    ;;
  gates)
    RD="${4:-$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}}"
    python3 "$ROOT/tools/l2_explore_battery/gate_check.py" --round-dir "$RD" --gate all
    ;;
  *)
    echo "Usage: $0 {probe|l1|graph|f1|all|gates} [LABEL] [ONLY_CASE] [FROM_OR_ROUND_DIR]"
    exit 2
    ;;
esac
