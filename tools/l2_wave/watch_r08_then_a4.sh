#!/usr/bin/env bash
# Espera DONE de sense16/round_08 (o que muera la batería) y lanza A4 N=5.
# No toca r08. Respeta meta/STOP (solo encola A4).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
S16="$ROOT/.tuide/ai/l2_wave/sense16"
R08="$S16/round_08"
NIGHT="$ROOT/.tuide/ai/l2_wave/v2_night"
LOG="$NIGHT/watch_r08_a4.log"
PIDFILE="$NIGHT/watch_r08_a4.pid"
mkdir -p "$NIGHT"

alive() { local p="${1:-}"; [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; }

if [[ "${WATCH_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "watch already running pid $old"
      exit 0
    fi
  fi
  export WATCH_DETACHED=1
  setsid "$0" "$@" >>"$LOG" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "watch_r08→A4 detached pid $!"
  exit 0
fi

echo $$ > "$PIDFILE"
echo "==== watch start $(date -Iseconds) ===="

while true; do
  if [[ -f "$R08/DONE" ]]; then
    echo "r08 DONE $(date -Iseconds)"
    break
  fi
  p=""
  if [[ -f "$R08/battery.pid" ]]; then
    p="$(tr -d ' \n' < "$R08/battery.pid" || true)"
  fi
  if [[ -n "$p" ]] && ! alive "$p"; then
    if [[ -f "$R08/DONE" ]]; then
      echo "r08 DONE after battery exit $(date -Iseconds)"
      break
    fi
    echo "battery dead without DONE $(date -Iseconds); waiting 60s then A4 anyway if still no DONE"
    sleep 60
    if [[ -f "$R08/DONE" ]]; then
      break
    fi
    echo "proceeding to A4 without DONE (battery dead)"
    break
  fi
  sleep 30
done

# Asegurar STOP (no r09 sense)
mkdir -p "$ROOT/.tuide/ai/l2_wave/meta"
date -Is > "$ROOT/.tuide/ai/l2_wave/meta/STOP"

echo "launching A4 $(date -Iseconds)"
bash "$ROOT/tools/l2_wave/run_a4_baseline.sh" 5 "$NIGHT/A4"
echo "==== watch end $(date -Iseconds) ===="
rm -f "$PIDFILE"
# sentinel para el agente CLI
printf '%s %s\n' "AGENT_LOOP_WAKE_v2night" \
  '{"prompt":"Lee y ejecuta tools/l2_wave/night_v2_tick_prompt.txt (noche plan A).","why":"r08_done_a4_launched"}'
