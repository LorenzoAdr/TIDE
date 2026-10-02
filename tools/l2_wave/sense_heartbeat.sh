#!/usr/bin/env bash
# Un disparo: duerme y despierta al supervisor. Rearmar en CADA tick.
# No setsid: Cursor tiene que ver el stdout (notify_on_output).
set -euo pipefail
SEC="${1:-1500}"
SENTINEL="${2:-AGENT_LOOP_WAKE_sense15}"
PROMPT="${3:-Lee y ejecuta tools/l2_wave/sense15_tick_prompt.txt (supervisión L2-wave).}"
sleep "$SEC"
printf '%s %s\n' "$SENTINEL" "{\"prompt\":\"$PROMPT\",\"why\":\"heartbeat\"}"
