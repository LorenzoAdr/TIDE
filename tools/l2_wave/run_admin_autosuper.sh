#!/usr/bin/env bash
# Self-supervised admin+verify battery pass.
# Runs curated probes, writes SCOREBOARD, then prints wake sentinel for the agent loop.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
API="${ADMIN_AUTOSUPER_API:-http://192.168.64.1:8080/v1}"
MODEL="${ADMIN_AUTOSUPER_MODEL:-qwen3.6-27b-q8_0}"
CASES="${ADMIN_AUTOSUPER_CASES:-$ROOT/tools/l2_wave/admin_autosuper_cases.json}"
BASE="${ADMIN_AUTOSUPER_BASE:-$ROOT/.tuide/ai/l2_admin_probe/autosuper}"
PASS_ID="${1:-}"
if [[ -z "$PASS_ID" ]]; then
  PASS_ID="pass_$(date -u +%Y%m%dT%H%M%SZ)"
fi
OUT="$BASE/$PASS_ID"
mkdir -p "$OUT"
echo "ADMIN_AUTOSUPER start pass=$PASS_ID out=$OUT api=$API model=$MODEL"
echo "$PASS_ID" >"$OUT/PASS_ID"
date -u +%Y-%m-%dT%H:%M:%SZ >"$OUT/STARTED"
cd "$ROOT"
python3 -u tools/l2_wave/run_admin_explore_battery.py \
  --cases "$CASES" \
  --out "$OUT" \
  --api "$API" \
  --model "$MODEL" \
  --max-turns 16 \
  --max-explore 4 \
  2>&1 | tee "$OUT/battery.console.log"
ec=${PIPESTATUS[0]}
date -u +%Y-%m-%dT%H:%M:%SZ >"$OUT/FINISHED"
echo "exit_code=$ec" >"$OUT/EXIT"
# Sentinel for Cursor agent wake (self-supervised loop).
echo "AGENT_ADMIN_LOOP_WAKE pass=$PASS_ID out=$OUT exit=$ec"
exit "$ec"
