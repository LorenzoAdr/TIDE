#!/usr/bin/env bash
# Wait genérico. ROUND BASE [SENTINEL] [TICK_PROMPT]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ROUND="${1:?round id, p.ej. 01}"
BASE="${2:?base dir, p.ej. .../sense15}"
SENTINEL="${3:-AGENT_LOOP_WAKE_sense15}"
TICK="${4:-tools/l2_wave/sense15_tick_prompt.txt}"
OUT="$BASE/round_$ROUND"
DONE="$OUT/DONE"
PIDFILE="$OUT/battery.pid"
SENSE="$(basename "$BASE")"

wake() {
  local why="$1"
  echo "${SENTINEL} {\"prompt\":\"Lee y ejecuta ${TICK} (supervisión L2-wave).\",\"round\":\"$ROUND\",\"why\":\"$why\"}"
  exit 0
}

alive() {
  local p="${1:-}"
  [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null
}

battery_live() {
  if [[ -f "$PIDFILE" ]]; then
    local p
    p="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$p"; then
      return 0
    fi
  fi
  pgrep -f "l2_wave_sense_battery.sh ${ROUND} .*${SENSE}" >/dev/null 2>&1 && return 0
  pgrep -f "${SENSE}/round_${ROUND}/" >/dev/null 2>&1 && return 0
  return 1
}

while true; do
  if [[ -f "$DONE" ]]; then
    wake DONE
  fi
  if ! battery_live; then
    if [[ -f "$OUT/STARTED.txt" ]] || [[ -f "$PIDFILE" ]]; then
      wake battery_dead_without_DONE
    fi
  fi
  sleep 20
done
