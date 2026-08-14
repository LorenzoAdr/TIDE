#!/usr/bin/env bash
# Stem-boost battery: label identifies the round (t01, t02, …).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:?label e.g. t01}"
CLI="$ROOT/build/stem_boost_battery"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts.json"
OUT="$ROOT/.tuide/ai/stem_boost_battery/round_${LABEL}"
mkdir -p "$OUT"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target stem_boost_battery -j"$(nproc)"
fi
echo "==== stem_boost $LABEL $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
"$CLI" --workspace "$ROOT" --prompts "$PROMPTS" --out "$OUT" --label "$LABEL" | tee "$OUT/console.log"
echo "wrote $OUT/summary.json"
