#!/usr/bin/env bash
# Batería 2×2: inducciones genéricas al piloto (existencia vs vacío).
# No recorta do legales. Sequential: mismo LLM.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-induce1}"
MANIFEST="$ROOT/tests/fixtures/l2_wave/induce_battery.json"
OUT="$ROOT/.tuide/ai/l2_wave/round_${LABEL}"
CLI="$ROOT/build/l2_harness_cli"
SCORE="$ROOT/tools/l2_wave/score_induce_battery.py"

mkdir -p "$OUT"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target l2_harness_cli -j"$(nproc)"
fi

echo "==== induce battery ($LABEL) $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
echo "manifest=$MANIFEST" | tee -a "$OUT/STARTED.txt"

python3 - "$MANIFEST" "$OUT" "$CLI" "$SCORE" <<'PY'
import json, os, subprocess, sys
from pathlib import Path

manifest = json.loads(Path(sys.argv[1]).read_text())
out = Path(sys.argv[2])
cli = sys.argv[3]
score = sys.argv[4]
log = out / "battery.log"
for induce in manifest["induces"]:
    for case in manifest["cases"]:
        name = f"{induce['id']}__{case['id']}"
        run = out / name
        if (run / "control.json").is_file():
            print(f"skip {name} (ya hay control.json)", flush=True)
            continue
        run.mkdir(parents=True, exist_ok=True)
        cmd = [
            cli, "wave-explore", "--control", "--run-jobs",
            "--induce", induce["id"],
            "--case", case["id"],
            "--out", str(run),
        ]
        print("RUN " + " ".join(cmd), flush=True)
        with open(out / f"{name}_stderr.log", "w") as err, open(
            out / f"{name}_stdout.json", "w"
        ) as outf:
            proc = subprocess.run(cmd, stdout=outf, stderr=err)
        with open(log, "a") as f:
            f.write(f"{name} exit={proc.returncode}\n")
        print(f"done {name} exit={proc.returncode}", flush=True)
subprocess.check_call([sys.executable, score, str(out), str(Path(sys.argv[1]))])
PY

echo "==== induce battery done $(date -Iseconds) ====" | tee -a "$OUT/STARTED.txt"
echo "scoreboard $OUT/scoreboard.json"
