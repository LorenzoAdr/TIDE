#!/usr/bin/env bash
# Despierta al agente si la ronda sense15 terminó (DONE) o si la batería murió sin DONE.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ROUND="${1:?round id, p.ej. 01}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/sense15}"
OUT="$BASE/round_$ROUND"
DONE="$OUT/DONE"
PIDFILE="$OUT/battery.pid"

wake() {
  local why="$1"
  echo "AGENT_LOOP_WAKE_sense15 {\"prompt\":\"Lee y ejecuta tools/l2_wave/sense15_tick_prompt.txt (supervisión sense15).\",\"round\":\"$ROUND\",\"why\":\"$why\"}"
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
  pgrep -f "l2_wave_sense_battery.sh ${ROUND} .*sense15" >/dev/null 2>&1 && return 0
  pgrep -f "sense15/round_${ROUND}/" >/dev/null 2>&1 && return 0
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
