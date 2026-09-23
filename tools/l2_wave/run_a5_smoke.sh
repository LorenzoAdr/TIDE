#!/usr/bin/env bash
# A5 smoke: N=1 sobre 5 casos train nuevos (no held-out).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
N="${1:-1}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/v2_night/A5_SMOKE}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery_a5_smoke.json"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"
RUNNER="$ROOT/tools/l2_wave/sense12_run_round.py"
mkdir -p "$BASE"
LOG="$BASE/queue.log"
PIDFILE="$BASE/queue.pid"

alive() { local p="${1:-}"; [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; }

if [[ "${A5_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "A5 smoke already running pid $old"
      exit 0
    fi
  fi
  export A5_DETACHED=1
  setsid "$0" "$@" >>"$LOG" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "A5 smoke detached pid $! n=$N base=$BASE"
  exit 0
fi

echo $$ > "$PIDFILE"
echo "==== A5 smoke start $(date -Iseconds) n=$N diet=minimal cases=5_train_new ====" | tee -a "$BASE/STARTED.txt"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

for i in $(seq -w 1 "$N"); do
  OUT="$BASE/rep_$i"
  mkdir -p "$OUT"
  if [[ -f "$OUT/DONE" ]]; then
    echo "skip rep_$i (DONE)" | tee -a "$LOG"
    continue
  fi
  echo "==== A5 smoke rep_$i $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
  python3 "$RUNNER" "$MANIFEST" "$OUT" "$CLI" "$SCORE" "$i" --control-diet minimal || {
    echo "rep_$i failed exit=$?" | tee -a "$LOG"
    echo "FAILED $(date -Iseconds)" > "$OUT/FAILED"
    continue
  }
  echo "DONE rep_$i $(date -Iseconds)" | tee -a "$LOG"
done

python3 "$ROOT/tools/l2_wave/summarize_a4.py" "$BASE" || true
if [[ -f "$BASE/A4_summary.json" ]]; then
  mv -f "$BASE/A4_summary.json" "$BASE/A5_SMOKE_summary.json"
fi

echo "==== A5 smoke done $(date -Iseconds) ====" | tee -a "$BASE/STARTED.txt"
touch "$BASE/DONE"
rm -f "$PIDFILE"
printf '%s %s\n' "AGENT_LOOP_WAKE_v2next" \
  '{"prompt":"Lee y ejecuta tools/l2_wave/v2_next_tick_prompt.txt (post-15h: A5 smoke / H / E).","why":"a5_smoke_done"}'
