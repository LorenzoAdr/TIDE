#!/usr/bin/env bash
# A0 tranche rerank — sin LLM: slice map-order vs rerank por ficha ES (card body).
# Uso: ./tools/l2_a0_tranche_rank_run.sh [LABEL] CASE_ID [MAX_CARDS]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-a0_card_rerank_v1}"
CASE_ID="${2:?falta CASE_ID}"
MAX_CARDS="${3:-12}"
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
  echo "==== A0 card rerank (no LLM) $LABEL $(date -Iseconds) ===="
  echo "case=$CASE_ID max_cards=$MAX_CARDS"
} | tee "$OUT/STARTED.txt" | tee "$CASE_DIR/meta.txt"

python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true

MAP_OUT="$CASE_DIR/map_last.md"
SEEDS_OUT="$CASE_DIR/seeds.json"
"$TUIDE" l1-debug --no-stem-embed --workspace "$ROOT" --query "$PROMPT" \
  --map-out "$MAP_OUT" --seeds-out "$SEEDS_OUT" >"$CASE_DIR/l1_gen.txt" 2>&1
cp -f "$MAP_OUT" "$ROOT/.tuide/ai/map_last.md"

SEEDS_ARG=""
if [[ -f "$SEEDS_OUT" ]]; then
  SEEDS_ARG="--seeds $SEEDS_OUT"
fi
# shellcheck disable=SC2086
"$L2_CLI" bootstrap $SEEDS_ARG "$PROMPT" >"$CASE_DIR/bootstrap.txt" 2>&1

RANK_OUT="$CASE_DIR/a0_tranche_rank"
mkdir -p "$RANK_OUT"
"$L2_CLI" a0-tranche-rank-shot --case "$CASE_ID" --max-cards "$MAX_CARDS" \
  --out-dir "$RANK_OUT" 2>&1 | tee "$CASE_DIR/rank.log"

echo "done → $CASE_DIR" | tee -a "$OUT/STARTED.txt"
