#!/usr/bin/env bash
# Prueba B — L2 explore con mapas del pipeline L1 real (L2 two-pass + rerank) + run-explore.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-nl_explore_l2twopass}"
START_AT="${2:-11_restore_session_state}"
ONLY_ONE="${3:-}"  # "only" → ejecutar solo START_AT (no 17..20)
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}"
TUIDE="$ROOT/build/tuide"
L2_CLI="$ROOT/build/l2_harness_cli"
mkdir -p "$OUT"
if [[ ! -f "$OUT/results.jsonl" ]]; then
  : >"$OUT/results.jsonl"
fi

if [[ ! -x "$TUIDE" || ! -x "$L2_CLI" ]]; then
  cmake --build "$ROOT/build" --target tuide l2_harness_cli -j"$(nproc)"
fi

CASE_IDS=$(python3 - <<PY
import json
cases = json.load(open("$PROMPTS"))
start = "$START_AT"
only_one = "$ONLY_ONE".lower() in ("only", "1", "true", "yes")
if only_one:
    ids = [start] if any(c["id"] == start for c in cases) else []
else:
    keep = False
    ids = []
    for c in cases:
        if c["id"] == start:
            keep = True
        if keep:
            ids.append(c["id"])
print("\n".join(ids))
PY
)
N=$(echo "$CASE_IDS" | wc -l)

echo "==== L2 explore battery ($LABEL) $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
echo "cases=$N start_at=$START_AT only_one=${ONLY_ONE:-0} prompts=$PROMPTS" | tee -a "$OUT/STARTED.txt"

fuser -k 18765/tcp >/dev/null 2>&1 || true
fuser -k 18766/tcp >/dev/null 2>&1 || true

while IFS= read -r ID; do
  [[ -z "$ID" ]] && continue
  PROMPT=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$ID'); print(c['prompt'])")
  CASE_DIR="$OUT/$ID"
  mkdir -p "$CASE_DIR"
  echo "==== CASE $ID $(date -Iseconds) ====" | tee "$CASE_DIR/meta.txt"

  python3 "$ROOT/tools/l2_battery/kill_l2_runtime.py" >/dev/null 2>&1 || true
  fuser -k 18765/tcp >/dev/null 2>&1 || true
  fuser -k 18766/tcp >/dev/null 2>&1 || true

  MAP_OUT="$CASE_DIR/map_last.md"
  SEEDS_OUT="$CASE_DIR/seeds.json"
  set +e
  "$TUIDE" l1-debug --no-stem-embed --workspace "$ROOT" --query "$PROMPT" \
    --map-out "$MAP_OUT" --seeds-out "$SEEDS_OUT" >"$CASE_DIR/l1_gen.txt" 2>&1
  L1_RC=$?
  set -e
  if [[ $L1_RC -ne 0 || ! -f "$MAP_OUT" ]]; then
    echo "{\"id\":\"$ID\",\"exit\":1,\"error\":\"l1_map\"}" >>"$OUT/results.jsonl"
    echo "  WARN: l1-debug failed rc=$L1_RC, skipping case"
    continue
  fi

  cp -f "$MAP_OUT" "$ROOT/.tuide/ai/map_last.md"

  set +e
  SEEDS_ARG=""
  if [[ -f "$SEEDS_OUT" ]]; then
    SEEDS_ARG="--seeds $SEEDS_OUT"
  fi
  # shellcheck disable=SC2086
  "$L2_CLI" bootstrap $SEEDS_ARG "$PROMPT" >"$CASE_DIR/bootstrap.txt" 2>&1
  BOOT_RC=$?
  if [[ $BOOT_RC -ne 0 ]]; then
    echo "{\"id\":\"$ID\",\"exit\":1,\"error\":\"bootstrap\"}" >>"$OUT/results.jsonl"
    set -e
    continue
  fi

  LOG="$CASE_DIR/run.log"
  "$L2_CLI" run-explore 2>&1 | tee "$LOG"
  RC=${PIPESTATUS[0]}
  set -e

  "$L2_CLI" status >"$CASE_DIR/status.txt" 2>&1 || true
  cp -f .tuide/ai/l2/state.json "$CASE_DIR/state.json" 2>/dev/null || true
  cp -f .tuide/ai/l2/session.md "$CASE_DIR/session.md" 2>/dev/null || true
  cp -f .tuide/ai/l2/pack.md "$CASE_DIR/pack.md" 2>/dev/null || true
  cp -f .tuide/ai/l2/a_state.json "$CASE_DIR/a_state.json" 2>/dev/null || true
  cp -f .tuide/ai/l2/a_notes.md "$CASE_DIR/a_notes.md" 2>/dev/null || true

  python3 - <<PY >>"$OUT/results.jsonl"
import json, re, pathlib
case = "$ID"
log = pathlib.Path("$LOG").read_text(errors="replace") if pathlib.Path("$LOG").exists() else ""
st = {}
try:
    st = json.loads(pathlib.Path("$CASE_DIR/state.json").read_text())
except Exception:
    pass
phase_line = next((ln for ln in log.splitlines() if ln.startswith("run-explore ok=")), "")
explore_ok = " ok=1 " in (" " + phase_line + " ") and "phase=explore_ok" in phase_line
steps = 0
m = re.search(r"steps=(\d+)", phase_line)
if m:
    steps = int(m.group(1))
row = {
    "id": case,
    "exit": int("$RC"),
    "ts": __import__("datetime").datetime.now().isoformat(timespec="seconds"),
    "explore_ok": explore_ok,
    "phase": st.get("phase"),
    "done": st.get("done"),
    "has_pack": st.get("has_pack"),
    "pack_incomplete": st.get("pack_incomplete"),
    "last_action": st.get("last_action"),
    "turn": st.get("turn"),
    "steps": steps,
    "watchlist": st.get("watchlist") or [],
    "run_line": phase_line.strip(),
}
print(json.dumps(row, ensure_ascii=False))
PY

  echo "==== DONE $ID rc=$RC ====" | tee -a "$CASE_DIR/meta.txt"
done <<< "$CASE_IDS"

python3 "$ROOT/tools/l2_explore_battery/score_explore.py" \
  --cases "$PROMPTS" --round-dir "$OUT" | tee "$OUT/score_console.txt"

echo "finished $(date -Iseconds)" | tee "$OUT/FINISHED.txt"
