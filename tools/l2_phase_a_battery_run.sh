#!/usr/bin/env bash
# Phase A-only battery — L1 map + bootstrap + run-explore-a (para en a_done, sin pack B).
# Uso:
#   ./tools/l2_phase_a_battery_run.sh [LABEL] [START_AT] [only]
# Ejemplos:
#   ./tools/l2_phase_a_battery_run.sh hybrid_a20_v1
#   ./tools/l2_phase_a_battery_run.sh hybrid_a20_v1 10_performance_threads_panel
#   ./tools/l2_phase_a_battery_run.sh smoke_a CASE_ID only
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-hybrid_a20_v1}"
START_AT="${2:-}"
ONLY_ONE="${3:-}"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}"
TUIDE="$ROOT/build/tuide"
L2_CLI="$ROOT/build/l2_harness_cli"
mkdir -p "$OUT"
if [[ ! -f "$OUT/results.jsonl" ]]; then
  : >"$OUT/results.jsonl"
fi

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export TUIDE_ROOT="$ROOT"

if [[ ! -x "$TUIDE" || ! -x "$L2_CLI" ]]; then
  cmake --build "$ROOT/build" --target tuide l2_harness_cli -j"$(nproc)"
fi

CASE_IDS=$(python3 - <<PY
import json
cases = json.load(open("$PROMPTS"))
start = "$START_AT".strip()
only_one = "$ONLY_ONE".lower() in ("only", "1", "true", "yes")
if not start:
    start = cases[0]["id"] if cases else ""
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
N=$(echo "$CASE_IDS" | grep -c . || true)

{
  echo "==== L2 Phase A battery ($LABEL) $(date -Iseconds) ===="
  echo "cases=$N start_at=${START_AT:-first} only_one=${ONLY_ONE:-0} prompts=$PROMPTS"
  echo "L2_FEAT_L2_EXPLORE_PHASE_A=$L2_FEAT_L2_EXPLORE_PHASE_A"
  echo "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=$L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY"
  echo "L1_HYBRID_EMBED_OFF=${L1_HYBRID_EMBED_OFF:-}"
  echo "L1_ES_CARD_EMBED_OFF=${L1_ES_CARD_EMBED_OFF:-}"
} | tee "$OUT/STARTED.txt"

while IFS= read -r ID; do
  [[ -z "$ID" ]] && continue
  # Skip cases already scored in results.jsonl (resume-friendly)
  if python3 -c "import json,sys; p='$OUT/results.jsonl';
ids={json.loads(l)['id'] for l in open(p) if l.strip()};
sys.exit(0 if '$ID' in ids else 1)" 2>/dev/null; then
    echo "==== SKIP $ID (already in results.jsonl) ====" | tee -a "$OUT/STARTED.txt"
    continue
  fi

  PROMPT=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$ID'); print(c['prompt'])")
  EXPECTED=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$ID'); print(','.join(c.get('expected_stems') or []))")
  CASE_DIR="$OUT/$ID"
  mkdir -p "$CASE_DIR"
  {
    echo "==== CASE $ID $(date -Iseconds) ===="
    echo "expected_stems=$EXPECTED"
    echo "prompt=$PROMPT"
  } | tee "$CASE_DIR/meta.txt"

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
    echo "{\"id\":\"$ID\",\"exit\":1,\"error\":\"l1_map\",\"ts\":\"$(date -Iseconds)\"}" >>"$OUT/results.jsonl"
    echo "  WARN: l1-debug failed rc=$L1_RC, skipping case" | tee -a "$CASE_DIR/meta.txt"
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
  set -e
  if [[ $BOOT_RC -ne 0 ]]; then
    echo "{\"id\":\"$ID\",\"exit\":1,\"error\":\"bootstrap\",\"ts\":\"$(date -Iseconds)\"}" >>"$OUT/results.jsonl"
    echo "  WARN: bootstrap failed rc=$BOOT_RC" | tee -a "$CASE_DIR/meta.txt"
    continue
  fi

  PHASE=$(python3 -c "import json; print(json.load(open('.tuide/ai/l2/state.json')).get('phase',''))" 2>/dev/null || true)
  echo "bootstrap phase=$PHASE" | tee -a "$CASE_DIR/meta.txt"

  LOG="$CASE_DIR/run.log"
  set +e
  "$L2_CLI" run-explore-a 2>&1 | tee "$LOG" | tee -a "$OUT/console.log"
  RC=${PIPESTATUS[0]}
  set -e

  "$L2_CLI" status >"$CASE_DIR/status.txt" 2>&1 || true
  cp -f .tuide/ai/l2/state.json "$CASE_DIR/state.json" 2>/dev/null || true
  cp -f .tuide/ai/l2/session.md "$CASE_DIR/session.md" 2>/dev/null || true
  cp -f .tuide/ai/l2/a_state.json "$CASE_DIR/a_state.json" 2>/dev/null || true
  cp -f .tuide/ai/l2/a_notes.md "$CASE_DIR/a_notes.md" 2>/dev/null || true
  cp -f .tuide/ai/l2/pack.md "$CASE_DIR/pack.md" 2>/dev/null || true

  python3 - <<PY >>"$OUT/results.jsonl"
import json, re, pathlib
from datetime import datetime
case = "$ID"
case_dir = pathlib.Path("$CASE_DIR")
log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
st, ast = {}, {}
try:
    st = json.loads((case_dir / "state.json").read_text())
except Exception:
    pass
try:
    ast = json.loads((case_dir / "a_state.json").read_text())
except Exception:
    pass
expected = [s for s in "$EXPECTED".split(",") if s]
loci = ast.get("loci_draft") or []
loci_stems = {str(x.get("stem") or "") for x in loci if isinstance(x, dict)}
hits = [s for s in expected if s in loci_stems or any(s in str(x.get("anchor") or "") for x in loci)]
a_done_ok = ("Phase A OK" in log) or ("explore_a_ok" in log) or bool(ast.get("done"))
coverage_fail = len(re.findall(r"faltan veredictos", log))
plan_reject = len(re.findall(r"plan rechazado en explore_a", log))
tool_reject = len(re.findall(r"tool ignorado en explore_a", log))
a_judge_n = len(re.findall(r"L2 ▸ a_judge", log))
phase_line = next((ln for ln in log.splitlines() if ln.startswith("run-explore-a")), "")
row = {
    "id": case,
    "exit": int("$RC"),
    "ts": datetime.now().isoformat(timespec="seconds"),
    "a_done_ok": bool(a_done_ok),
    "phase": st.get("phase"),
    "done": st.get("done") or ast.get("done"),
    "last_action": st.get("last_action"),
    "turn": st.get("turn"),
    "peeks": ast.get("peeks_used"),
    "turns": ast.get("turns"),
    "expansions": ast.get("expansions"),
    "a0_turns": ast.get("a0_turns"),
    "cards_used": ast.get("cards_used"),
    "a_subphase": ast.get("a_subphase"),
    "queue_n": len(ast.get("queue") or []),
    "a1_queue_n": len(ast.get("a1_queue") or []),
    "loci_n": len(loci),
    "loci_hits": hits,
    "expected": expected,
    "a0_coverage_fail": coverage_fail,
    "plan_rejects": plan_reject,
    "tool_rejects": tool_reject,
    "a_judge_turns": a_judge_n,
    "run_line": phase_line.strip(),
}
print(json.dumps(row, ensure_ascii=False))
PY

  echo "==== DONE $ID rc=$RC ====" | tee -a "$CASE_DIR/meta.txt" | tee -a "$OUT/STARTED.txt"
done <<< "$CASE_IDS"

python3 "$ROOT/tools/l2_explore_battery/score_phase_a_battery.py" \
  --cases "$PROMPTS" --round-dir "$OUT" | tee "$OUT/score_console.txt"

echo "finished $(date -Iseconds)" | tee "$OUT/FINISHED.txt"
