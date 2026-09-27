#!/usr/bin/env bash
# Launch vibecode100 battery (setsid). Cortar: touch $OUT/STOP
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
API="${ADMIN_VIBECODE_API:-http://192.168.64.1:8080/v1}"
MODEL="${ADMIN_VIBECODE_MODEL:-qwen3.6-27b-q8_0}"
CASES="${ADMIN_VIBECODE_CASES:-$ROOT/tools/l2_wave/admin_vibecode_100.json}"
STAMP="${1:-$(date -u +%Y%m%dT%H%M%SZ)}"
OUT="${ADMIN_VIBECODE_OUT:-$ROOT/.tuide/ai/l2_admin_probe/vibecode100_$STAMP}"
mkdir -p "$OUT"
echo "$OUT" >"$OUT/OUT_PATH"
date -u +%Y-%m-%dT%H:%M:%SZ >"$OUT/LAUNCHED"
echo "VIBECODE100 out=$OUT api=$API model=$MODEL"
echo "STOP: touch $OUT/STOP"
cd "$ROOT"
exec python3 -u tools/l2_wave/run_admin_explore_battery.py \
  --cases "$CASES" \
  --out "$OUT" \
  --api "$API" \
  --model "$MODEL" \
  --max-turns 16 \
  --max-explore 4
