#!/usr/bin/env bash
# S0: N repeticiones con --control-diet minimal (prompt corto, sin Sello/Gate).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
N="${1:-5}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/v2_night/S0}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery.json"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"
RUNNER="$ROOT/tools/l2_wave/sense12_run_round.py"
mkdir -p "$BASE"
LOG="$BASE/queue.log"
PIDFILE="$BASE/queue.pid"

alive() { local p="${1:-}"; [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null; }

if [[ "${S0_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "S0 queue already running pid $old"
      exit 0
    fi
  fi
  export S0_DETACHED=1
  setsid "$0" "$@" >>"$LOG" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "S0 queue detached pid $! n=$N base=$BASE"
  exit 0
fi

echo $$ > "$PIDFILE"
echo "==== S0 queue start $(date -Iseconds) n=$N diet=minimal ====" | tee -a "$BASE/STARTED.txt"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

# smoke: prompt size
python3 - <<'PY' || true
import subprocess, re
# can't call C++ easily; check after first run via system.txt
print("S0: will verify prompt size from first control_1/system.txt")
PY

for i in $(seq -w 1 "$N"); do
  OUT="$BASE/rep_$i"
  mkdir -p "$OUT"
  if [[ -f "$OUT/DONE" ]]; then
    echo "skip rep_$i (DONE)" | tee -a "$LOG"
    continue
  fi
  echo "==== S0 rep_$i $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
  python3 "$RUNNER" "$MANIFEST" "$OUT" "$CLI" "$SCORE" "$i" --control-diet minimal || {
    echo "rep_$i failed exit=$?" | tee -a "$LOG"
    echo "FAILED $(date -Iseconds)" > "$OUT/FAILED"
    continue
  }
  echo "DONE rep_$i $(date -Iseconds)" | tee -a "$LOG"
done

python3 "$ROOT/tools/l2_wave/summarize_a4.py" "$BASE" || true
# rename summary key file for clarity
if [[ -f "$BASE/A4_summary.json" ]]; then
  mv -f "$BASE/A4_summary.json" "$BASE/S0_summary.json"
fi
# comparación vs A4 si existe
python3 "$ROOT/tools/l2_wave/compare_a4_s0.py" \
  "$ROOT/.tuide/ai/l2_wave/v2_night/A4" "$BASE" \
  -o "$ROOT/.tuide/ai/l2_wave/v2_night/S0_vs_A4.json" || true

echo "==== S0 queue done $(date -Iseconds) ====" | tee -a "$BASE/STARTED.txt"
touch "$BASE/DONE"
rm -f "$PIDFILE"
printf '%s %s\n' "AGENT_LOOP_WAKE_v2night" \
  '{"prompt":"Lee y ejecuta tools/l2_wave/night_v2_tick_prompt.txt (noche plan A).","why":"s0_done"}'
