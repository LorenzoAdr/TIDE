#!/usr/bin/env bash
# L2 battery runner — persists per-case artifacts for long runs / reconnect.
# Usage: ./tools/l2_battery_run.sh A|B|C|D
# Prompts: L2_BATTERY_PROMPTS, else tools/l2_battery/prompts_v1.json (or prompts_hard.json via env).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
ROUND="${1:?round name e.g. A|B|C|D}"
BAT="$ROOT/.tuide/ai/l2_battery"
DEFAULT_PROMPTS="$ROOT/tools/l2_battery/prompts_v1.json"
if [[ ! -f "$DEFAULT_PROMPTS" && -f "$BAT/prompts.json" ]]; then
  DEFAULT_PROMPTS="$BAT/prompts.json"
fi
PROMPTS="${L2_BATTERY_PROMPTS:-$DEFAULT_PROMPTS}"
OUT="$BAT/round_${ROUND}"
mkdir -p "$OUT"
RESULTS="$OUT/results.jsonl"
: >"$RESULTS"
CLI="$ROOT/build/l2_harness_cli"
test -x "$CLI"

restore_product() {
  git checkout -- src/ui src/util 2>/dev/null || true
}

# Avoid map_stale from a previous battery/prompt: align map_last query to this prompt.
seed_map_query() {
  local prompt="$1"
  local map=".tuide/ai/map_last.md"
  if [[ -f "$map" ]]; then
    python3 - "$map" "$prompt" <<'PY'
import sys
from pathlib import Path
p=Path(sys.argv[1]); prompt=sys.argv[2]
text=p.read_text(errors='replace')
lines=text.splitlines()
out=[]
replaced=False
for ln in lines:
    if ln.startswith('query:') and not replaced:
        out.append('query: '+prompt)
        replaced=True
    else:
        out.append(ln)
if not replaced:
    out.insert(0, 'query: '+prompt)
p.write_text('\n'.join(out)+('\n' if text.endswith('\n') else ''))
PY
  else
    printf 'query: %s\n\n## Ranked entries\n\n' "$prompt" >"$map"
  fi
}

n=$(python3 -c "import json; print(len(json.load(open('$PROMPTS'))))")
echo "L2 battery round=$ROUND cases=$n out=$OUT"
echo "started $(date -Iseconds)" | tee "$OUT/STARTED.txt"

for i in $(seq 0 $((n - 1))); do
  python3 - <<PY
import json
from pathlib import Path
p=json.load(open("$PROMPTS"))[$i]
Path("$OUT/current.json").write_text(json.dumps(p, indent=2)+"\n")
print(p["id"])
print(p["prompt"])
PY
  ID=$(python3 -c "import json; print(json.load(open('$PROMPTS'))[$i]['id'])")
  PROMPT=$(python3 -c "import json; print(json.load(open('$PROMPTS'))[$i]['prompt'])")
  CASE_DIR="$OUT/$ID"
  mkdir -p "$CASE_DIR"
  echo "==== CASE $ID $(date -Iseconds) ====" | tee "$CASE_DIR/meta.txt"
  restore_product
  seed_map_query "$PROMPT"
  python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null

  "$CLI" bootstrap "$PROMPT" >"$CASE_DIR/bootstrap.txt" 2>&1 || {
    echo "{\"id\":\"$ID\",\"ok\":false,\"error\":\"bootstrap\"}" >>"$RESULTS"
    continue
  }
  LOG="$CASE_DIR/run.log"
  set +e
  "$CLI" run 2>&1 | tee "$LOG"
  RC=${PIPESTATUS[0]}
  set -e
  "$CLI" status >"$CASE_DIR/status.txt" 2>&1 || true
  cp -f .tuide/ai/l2/state.json "$CASE_DIR/state.json" 2>/dev/null || true
  cp -f .tuide/ai/l2/session.md "$CASE_DIR/session.md" 2>/dev/null || true
  cp -f .tuide/ai/l2/debrief.md "$CASE_DIR/debrief.md" 2>/dev/null || true
  git status --short >"$CASE_DIR/git_status.txt" 2>&1 || true
  git diff --stat -- src/ui src/util >"$CASE_DIR/diff_stat.txt" 2>&1 || true
  git diff -- src/ui src/util >"$CASE_DIR/diff.patch" 2>&1 || true

  python3 - <<PY >>"$RESULTS"
import json, re, pathlib
from datetime import datetime
case = "$ID"
log = pathlib.Path("$LOG").read_text(errors="replace") if pathlib.Path("$LOG").exists() else ""
st = {}
try:
    st = json.loads(pathlib.Path("$CASE_DIR/state.json").read_text())
except Exception:
    pass
actions = re.findall(r"acción=([^\s]+)", log)
steps = [int(x) for x in re.findall(r"paso=(\d+)/", log)]
row = {
    "id": case,
    "exit": int("$RC"),
    "ts": datetime.now().isoformat(timespec="seconds"),
    "phase": st.get("phase"),
    "done": st.get("done"),
    "turn": st.get("turn"),
    "last_action": st.get("last_action"),
    "edit_fail_count": st.get("edit_fail_count"),
    "compile_attempt": st.get("compile_attempt"),
    "edit_attempt": st.get("edit_attempt"),
    "map_review": st.get("map_review"),
    "max_step": max(steps) if steps else 0,
    "actions": actions,
    "n_plan": actions.count("plan"),
    "n_edit": sum(1 for a in actions if a.startswith("edit")),
    "n_tools": sum(1 for a in actions if a.startswith("tool")),
    "n_compile_ok": log.count("OK compile"),
    "n_compile_fail": log.count("compile exit_code") + log.count("FAIL compile"),
    "n_mal_formado": log.count("mal formado"),
    "n_path_fix": log.count("path auto-corregido") + log.count("auto-corregido"),
    "n_tool_pushback": log.count("edit_phase_tool_pushback") + log.count("post_pack_tool_pushback") + log.count("tool_pushback"),
    "n_clarify": log.count("clarify"),
    "run_ok_line": next((ln for ln in log.splitlines() if ln.startswith("run ok=")), ""),
    "diff_stat": pathlib.Path("$CASE_DIR/diff_stat.txt").read_text(errors="replace").strip(),
    "product_dirty": bool(pathlib.Path("$CASE_DIR/diff_stat.txt").read_text(errors="replace").strip()),
}
print(json.dumps(row, ensure_ascii=False))
PY

  restore_product
  echo "==== DONE $ID rc=$RC $(date -Iseconds) ====" | tee -a "$CASE_DIR/meta.txt"
done

echo "finished $(date -Iseconds)" | tee "$OUT/FINISHED.txt"
python3 - <<PY
import json
from pathlib import Path
rows=[json.loads(l) for l in Path("$RESULTS").read_text().splitlines() if l.strip()]
Path("$OUT/summary.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False)+"\n")
print("wrote", "$OUT/summary.json", "n=", len(rows))
for r in rows:
    print(f"- {r['id']}: phase={r.get('phase')} done={r.get('done')} last={r.get('last_action')} steps={r.get('max_step')} dirty={r.get('product_dirty')} | {r.get('run_ok_line','')[:80]}")
PY
