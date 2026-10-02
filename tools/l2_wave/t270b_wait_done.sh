#!/usr/bin/env bash
# Espera DONE de T2_70B (cola run_s0_baseline).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BASE="${1:-$ROOT/.tuide/ai/l2_wave/v2_night/T2_70B}"
SENTINEL="${2:-AGENT_LOOP_WAKE_t270b}"
TICK="${3:-tools/l2_wave/t270b_tick_prompt.txt}"
DONE="$BASE/DONE"
PIDFILE="$BASE/queue.pid"
TAG="$(basename "$BASE")"

wake() {
  echo "${SENTINEL} {\"prompt\":\"Lee y ejecuta ${TICK}.\",\"why\":\"$1\"}"
  exit 0
}

alive() {
  local p="${1:-}"
  [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null
}

queue_alive() {
  if [[ -f "$PIDFILE" ]]; then
    local p
    p="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$p"; then
      return 0
    fi
  fi
  pgrep -f "$TAG" >/dev/null 2>&1 && return 0
  return 1
}

while true; do
  if [[ -f "$DONE" ]]; then
    wake DONE
  fi
  if ! queue_alive; then
    sleep 15
    if [[ -f "$DONE" ]]; then
      wake DONE
    fi
    if ! queue_alive; then
      wake queue_dead
    fi
  fi
  sleep 30
done
