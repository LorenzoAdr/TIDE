#!/usr/bin/env bash
# Prueba A — evaluación offline del ranking L1 (léxico + stem boost semántico).
# No usa LLM L1/L2; mide el pipeline de map_last (facets → shortlist → priors).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-explore_a}"
CLI="$ROOT/build/stem_boost_battery"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_explore_a.json"
OUT="$ROOT/.tuide/ai/stem_boost_battery/round_${LABEL}"
mkdir -p "$OUT"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target stem_boost_battery -j"$(nproc)"
fi
echo "==== Prueba A L1 ranking ($LABEL) $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
echo "prompts=$PROMPTS" | tee -a "$OUT/STARTED.txt"
"$CLI" --workspace "$ROOT" --prompts "$PROMPTS" --out "$OUT" --label "$LABEL" --verbose \
  | tee "$OUT/console.log"
echo "wrote $OUT/summary.json and $OUT/results.jsonl"
