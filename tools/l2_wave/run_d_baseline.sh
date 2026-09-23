#!/usr/bin/env bash
# D: N reps S0+D (B off, C off, job_cap D on). Ablación de B tras BD peor.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
N="${1:-5}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/v2_night/D}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery.json"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"
RUNNER="$ROOT/tools/l2_wave/sense12_run_round.py"
mkdir -p "$BASE"
LOG="$BASE/queue.log"
PIDFILE="$BASE/queue.pid"

alive() { local p="${1:-}"; [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; }

if [[ "${D_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "D queue already running pid $old"
      exit 0
    fi
  fi
  export D_DETACHED=1
  setsid "$0" "$@" >>"$LOG" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "D queue detached pid $! n=$N base=$BASE"
  exit 0
fi

echo $$ > "$PIDFILE"
echo "==== D queue start $(date -Iseconds) n=$N diet=minimal+D B=off C=off ====" | tee -a "$BASE/STARTED.txt"
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
  echo "==== D rep_$i $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
  python3 "$RUNNER" "$MANIFEST" "$OUT" "$CLI" "$SCORE" "$i" --control-diet minimal || {
    echo "rep_$i failed exit=$?" | tee -a "$LOG"
    echo "FAILED $(date -Iseconds)" > "$OUT/FAILED"
    continue
  }
  echo "DONE rep_$i $(date -Iseconds)" | tee -a "$LOG"
done

python3 "$ROOT/tools/l2_wave/summarize_a4.py" "$BASE" || true
if [[ -f "$BASE/A4_summary.json" ]]; then
  mv -f "$BASE/A4_summary.json" "$BASE/D_summary.json"
fi
python3 "$ROOT/tools/l2_wave/compare_a4_s0.py" \
  "$ROOT/.tuide/ai/l2_wave/v2_night/S0" "$BASE" \
  -o "$ROOT/.tuide/ai/l2_wave/v2_night/D_vs_S0.json" || true
python3 "$ROOT/tools/l2_wave/decide_vs_s0.py" \
  "$ROOT/.tuide/ai/l2_wave/v2_night/S0" "$BASE" --label D \
  -o "$ROOT/.tuide/ai/l2_wave/v2_night/DECISION_D.json" || true

echo "==== D queue done $(date -Iseconds) ====" | tee -a "$BASE/STARTED.txt"
touch "$BASE/DONE"
rm -f "$PIDFILE"
printf '%s %s\n' "AGENT_LOOP_WAKE_v215h" \
  '{"prompt":"Lee y ejecuta tools/l2_wave/v2_15h_tick_prompt.txt (noche 15h: post-D).","why":"d_done"}'
