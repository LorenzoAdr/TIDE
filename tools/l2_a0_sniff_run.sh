#!/usr/bin/env bash
# A0 Effect Summary — batería sin LLM (fichas + veredictos heurísticos).
# Uso: ./tools/l2_a0_sniff_run.sh [LABEL] CASE_ID
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-a0_sniff_v1}"
CASE_ID="${2:?falta CASE_ID}"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}"
TUIDE="$ROOT/build/tuide"
L2_CLI="$ROOT/build/l2_harness_cli"
mkdir -p "$OUT"

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export TUIDE_ROOT="$ROOT"

if [[ ! -x "$TUIDE" || ! -x "$L2_CLI" ]]; then
  cmake --build "$ROOT/build" --target tuide l2_harness_cli -j"$(nproc)"
fi

PROMPT=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$CASE_ID'); print(c['prompt'])")
EXPECTED=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$CASE_ID'); print(','.join(c.get('expected_stems') or []))")
CASE_DIR="$OUT/$CASE_ID"
mkdir -p "$CASE_DIR"

{
  echo "==== A0 sniff (no LLM) $LABEL $(date -Iseconds) ===="
  echo "case=$CASE_ID expected_stems=$EXPECTED"
  echo "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=$L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY"
} | tee "$OUT/STARTED.txt" | tee "$CASE_DIR/meta.txt"

python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

MAP_OUT="$CASE_DIR/map_last.md"
SEEDS_OUT="$CASE_DIR/seeds.json"
set +e
"$TUIDE" l1-debug --no-stem-embed --workspace "$ROOT" --query "$PROMPT" \
  --map-out "$MAP_OUT" --seeds-out "$SEEDS_OUT" >"$CASE_DIR/l1_gen.txt" 2>&1
L1_RC=$?
set -e
if [[ $L1_RC -ne 0 || ! -f "$MAP_OUT" ]]; then
  echo "FAIL: l1-debug rc=$L1_RC" | tee -a "$CASE_DIR/meta.txt"
  exit 1
fi
cp -f "$MAP_OUT" "$ROOT/.tuide/ai/map_last.md"

SEEDS_ARG=""
if [[ -f "$SEEDS_OUT" ]]; then
  SEEDS_ARG="--seeds $SEEDS_OUT"
fi
# shellcheck disable=SC2086
"$L2_CLI" bootstrap $SEEDS_ARG "$PROMPT" >"$CASE_DIR/bootstrap.txt" 2>&1

echo "---- effect-summary-probe (map top-20) ----" | tee "$CASE_DIR/effect_summary_map.txt"
"$L2_CLI" effect-summary-probe --from-map "$MAP_OUT" --top 20 \
  2>&1 | tee -a "$CASE_DIR/effect_summary_map.txt"

A_STATE="$ROOT/.tuide/ai/l2/a_state.json"
echo "---- effect-summary-probe (cola A0 tranche) ----" | tee "$CASE_DIR/effect_summary_tranche.txt"
"$L2_CLI" effect-summary-probe --from-a-state "$A_STATE" --tranche 14 \
  2>&1 | tee -a "$CASE_DIR/effect_summary_tranche.txt"

echo "---- a0-sniff-shot (heurístico) ----" | tee "$CASE_DIR/a0_sniff.log"
set +e
"$L2_CLI" a0-sniff-shot --case "$CASE_ID" --out-dir "$CASE_DIR/a0" --turns 4 \
  2>&1 | tee -a "$CASE_DIR/a0_sniff.log"
RC=${PIPESTATUS[0]}
set -e

cp -f "$A_STATE" "$CASE_DIR/a_state_after.json" 2>/dev/null || true

python3 - <<PY
import json, pathlib
case_dir = pathlib.Path("$CASE_DIR")
report_path = case_dir / "a0" / "report.json"
if report_path.exists():
    r = json.loads(report_path.read_text())
    print("---- report.json ----")
    for k in ("turns", "total_cards", "expands", "rejects", "uncertain",
              "expand_seed_hit_rate", "expected_stem_hits_in_expands", "cards_used", "cursor"):
        if k in r:
            print(f"  {k}={r[k]}")
else:
    print("(sin report.json)")
PY

echo "done rc=$RC artifacts → $CASE_DIR" | tee -a "$OUT/STARTED.txt"
exit "$RC"
