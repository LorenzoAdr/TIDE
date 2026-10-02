#!/usr/bin/env bash
# A4: N repeticiones de la batería sense congelada (miss-answer), en serie.
# setsid-friendly: sobrevive al agente. No lanza sense16 ni toca meta/STOP.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
N="${1:-5}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/v2_night/A4}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery.json"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"
RUNNER="$ROOT/tools/l2_wave/sense12_run_round.py"
mkdir -p "$BASE"
LOG="$BASE/queue.log"
PIDFILE="$BASE/queue.pid"

alive() { local p="${1:-}"; [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; }

if [[ "${A4_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "A4 queue already running pid $old"
      exit 0
    fi
  fi
  export A4_DETACHED=1
  setsid "$0" "$@" >>"$LOG" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "A4 queue detached pid $! n=$N base=$BASE"
  exit 0
fi

echo $$ > "$PIDFILE"
echo "==== A4 queue start $(date -Iseconds) n=$N ====" | tee -a "$BASE/STARTED.txt"
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
  # no pises una rep a medias si se reanuda
  echo "==== A4 rep_$i $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
  python3 "$RUNNER" "$MANIFEST" "$OUT" "$CLI" "$SCORE" "$i" || {
    echo "rep_$i failed exit=$?" | tee -a "$LOG"
    echo "FAILED $(date -Iseconds)" > "$OUT/FAILED"
    # sigue con la siguiente; no tumba la cola
    continue
  }
  echo "DONE rep_$i $(date -Iseconds)" | tee -a "$LOG"
done

python3 "$ROOT/tools/l2_wave/summarize_a4.py" "$BASE" || true
echo "==== A4 queue done $(date -Iseconds) ====" | tee -a "$BASE/STARTED.txt"
touch "$BASE/DONE"
rm -f "$PIDFILE"
