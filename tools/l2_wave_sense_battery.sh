#!/usr/bin/env bash
# Una ronda de 5 cazas (existe / hueco / absurdo) con induce miss-answer.
# Se desprende de la sesión de Cursor (setsid): si el agente/subagente muere,
# la ronda sigue. Un segundo launch con el mismo id no duplica: salta o reanuda.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ROUND="${1:?round id, p.ej. 01}"
BASE="${2:-$ROOT/.tuide/ai/l2_wave/sense12}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery.json"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"
OUT="$BASE/round_$ROUND"
PIDFILE="$OUT/battery.pid"

mkdir -p "$OUT"

alive() {
  local p="${1:-}"
  [[ -n "$p" ]] && kill -0 "$p" 2>/dev/null
}

# El chequeo de duplicado solo en el launcher. El hijo ve su propio pid en el
# pidfile y si corre aquí se sale con «already running» sin cazas (ronda 11).
if [[ "${SENSE12_DETACHED:-}" != "1" ]]; then
  if [[ -f "$PIDFILE" ]]; then
    old="$(tr -d ' \n' < "$PIDFILE" || true)"
    if alive "$old"; then
      echo "sense12 round $ROUND already running pid $old"
      exit 0
    fi
  fi
  export SENSE12_DETACHED=1
  setsid "$0" "$@" >>"$OUT/battery.console.log" 2>&1 < /dev/null &
  echo $! > "$PIDFILE"
  echo "sense12 round $ROUND detached pid $!"
  exit 0
fi

echo $$ > "$PIDFILE"
ln -sfn "round_$ROUND" "$BASE/CURRENT"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

echo "==== sense12 round $ROUND $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
python3 "$ROOT/tools/l2_wave/sense12_run_round.py" "$MANIFEST" "$OUT" "$CLI" "$SCORE" "$ROUND"
echo "==== sense12 round $ROUND done $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
