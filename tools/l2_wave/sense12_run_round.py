#!/usr/bin/env python3
"""Run one sense battery round. Optional extra CLI args after round_id (e.g. --control-diet minimal)."""
import json
import subprocess
import sys
from pathlib import Path

manifest = json.loads(Path(sys.argv[1]).read_text())
out = Path(sys.argv[2])
cli = sys.argv[3]
score = sys.argv[4]
round_id = sys.argv[5]
extra = sys.argv[6:]
induce = manifest.get("induce") or "miss-answer"
log = out / "battery.log"
for case in manifest["cases"]:
    cid = case["id"]
    run = out / cid
    if (run / "control.json").is_file():
        print(f"skip {cid}", flush=True)
        continue
    run.mkdir(parents=True, exist_ok=True)
    cmd = [cli, "wave-explore", "--control", "--run-jobs", "--induce", induce, "--out", str(run)]
    cmd += extra
    if case.get("via") == "prompt":
        cmd += ["--prompt", case["prompt"]]
    else:
        cmd += ["--case", cid]
    print("RUN " + " ".join(cmd[:10]) + f" ... {cid}", flush=True)
    with open(out / f"{cid}_stderr.log", "w") as err, open(out / f"{cid}_stdout.json", "w") as outf:
        proc = subprocess.run(cmd, stdout=outf, stderr=err)
    with open(log, "a") as f:
        f.write(f"{cid} exit={proc.returncode}\n")
    print(f"done {cid} exit={proc.returncode}", flush=True)
subprocess.check_call([sys.executable, score, str(out), str(Path(sys.argv[1]))])
(out / "DONE").write_text(f"round={round_id}\n", encoding="utf-8")
print(f"DONE {out}", flush=True)
