#!/usr/bin/env bash
# Primer turno A0 only — tranche Effect Summary → 1× a_judge con cobertura completa.
# Uso: ./tools/l2_a0_first_judge_run.sh [LABEL] [CASE_ID] [MAX_CARDS]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-a0_first_judge_v1}"
CASE_ID="${2:-17_ai_spinner_stuck}"
MAX_CARDS="${3:-8}"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}"
TUIDE="$ROOT/build/tuide"
L2_CLI="$ROOT/build/l2_harness_cli"
CASE_DIR="$OUT/$CASE_ID"
mkdir -p "$CASE_DIR"

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export TUIDE_ROOT="$ROOT"

if [[ ! -x "$TUIDE" || ! -x "$L2_CLI" ]]; then
  cmake --build "$ROOT/build" --target tuide l2_harness_cli -j"$(nproc)"
fi

PROMPT=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$CASE_ID'); print(c['prompt'])")
{
  echo "==== A0 first judge $LABEL $(date -Iseconds) ===="
  echo "case=$CASE_ID max_cards=$MAX_CARDS"
  echo "prompt=$PROMPT"
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

SHOT_OUT="$CASE_DIR/a0_first"
mkdir -p "$SHOT_OUT"

echo "---- dry tranche (max-cards=$MAX_CARDS) ----" | tee "$CASE_DIR/dry.log"
"$L2_CLI" a0-first-judge-shot --case "$CASE_ID" --max-cards "$MAX_CARDS" --dry \
  --out-dir "$SHOT_OUT/dry" 2>&1 | tee -a "$CASE_DIR/dry.log"

echo "---- LLM first A0 judge ----" | tee "$CASE_DIR/run.log"
set +e
"$L2_CLI" a0-first-judge-shot --case "$CASE_ID" --max-cards "$MAX_CARDS" \
  --out-dir "$SHOT_OUT/llm" --apply 2>&1 | tee -a "$CASE_DIR/run.log"
RC=${PIPESTATUS[0]}
set -e

cp -f "$ROOT/.tuide/ai/l2/a_state.json" "$CASE_DIR/a_state_after.json" 2>/dev/null || true

python3 - <<PY
import json, pathlib
case_dir = pathlib.Path("$CASE_DIR")
cov_path = case_dir / "a0_first/llm/coverage.json"
if cov_path.exists():
    c = json.loads(cov_path.read_text())
    print("---- coverage.json ----")
    for k in ("shown", "covered", "verdicts", "full", "busy_strip_in_tranche", "apply_ok"):
        if k in c:
            print(f"  {k}={c[k]}")
    missing = c.get("missing") or []
    if missing:
        print("  missing_targets:")
        for m in missing:
            print(f"    - {m}")
else:
    print("(sin coverage.json)")
PY

echo "done rc=$RC → $CASE_DIR" | tee -a "$OUT/STARTED.txt"
exit "$RC"
