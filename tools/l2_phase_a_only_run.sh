#!/usr/bin/env bash
# Phase A only — L1 map + bootstrap + run-explore-a (para en a_done, sin pack B).
# Uso: ./tools/l2_phase_a_only_run.sh [LABEL] CASE_ID
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-phase_a_only_v1}"
CASE_ID="${2:?falta CASE_ID}"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/l2_explore_battery/round_${LABEL}"
TUIDE="$ROOT/build/tuide"
L2_CLI="$ROOT/build/l2_harness_cli"
mkdir -p "$OUT"

export L2_FEAT_L2_EXPLORE_PHASE_A=1
export L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1
export TUIDE_ROOT="$ROOT"

if [[ ! -x "$TUIDE" || ! -x "$L2_CLI" ]]; then
  cmake --build "$ROOT/build" --target tuide l2_harness_cli -j"$(nproc)"
fi

PROMPT=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$CASE_ID'); print(c['prompt'])")
EXPECTED=$(python3 -c "import json; c=next(x for x in json.load(open('$PROMPTS')) if x['id']=='$CASE_ID'); print(','.join(c.get('expected_stems') or []))")
CASE_DIR="$OUT/$CASE_ID"
mkdir -p "$CASE_DIR"

{
  echo "==== Phase A only ($LABEL) $(date -Iseconds) ===="
  echo "case=$CASE_ID expected_stems=$EXPECTED"
  echo "prompt=$PROMPT"
  echo "L2_FEAT_L2_EXPLORE_PHASE_A=$L2_FEAT_L2_EXPLORE_PHASE_A"
  echo "L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=$L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY"
} | tee "$OUT/STARTED.txt" | tee "$CASE_DIR/meta.txt"

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
  echo "FAIL: l1-debug rc=$L1_RC" | tee -a "$CASE_DIR/meta.txt"
  exit 1
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
  echo "FAIL: bootstrap rc=$BOOT_RC" | tee -a "$CASE_DIR/meta.txt"
  cat "$CASE_DIR/bootstrap.txt"
  exit 1
fi

# Confirm Phase A bootstrap
PHASE=$(python3 -c "import json; print(json.load(open('.tuide/ai/l2/state.json')).get('phase',''))" 2>/dev/null || true)
echo "bootstrap phase=$PHASE" | tee -a "$CASE_DIR/meta.txt"
if [[ "$PHASE" != "explore_a" ]]; then
  echo "WARN: expected explore_a after bootstrap, got '$PHASE'" | tee -a "$CASE_DIR/meta.txt"
fi

LOG="$CASE_DIR/run.log"
set +e
"$L2_CLI" run-explore-a 2>&1 | tee "$LOG" | tee "$OUT/console.log"
RC=${PIPESTATUS[0]}
set -e

"$L2_CLI" status >"$CASE_DIR/status.txt" 2>&1 || true
cp -f .tuide/ai/l2/state.json "$CASE_DIR/state.json" 2>/dev/null || true
cp -f .tuide/ai/l2/session.md "$CASE_DIR/session.md" 2>/dev/null || true
cp -f .tuide/ai/l2/a_state.json "$CASE_DIR/a_state.json" 2>/dev/null || true
cp -f .tuide/ai/l2/a_notes.md "$CASE_DIR/a_notes.md" 2>/dev/null || true
cp -f .tuide/ai/l2/pack.md "$CASE_DIR/pack.md" 2>/dev/null || true

python3 - <<PY
import json, pathlib, re
case_dir = pathlib.Path("$CASE_DIR")
log = (case_dir / "run.log").read_text(errors="replace") if (case_dir / "run.log").exists() else ""
ast = {}
try:
    ast = json.loads((case_dir / "a_state.json").read_text())
except Exception:
    pass
expected = [s for s in "$EXPECTED".split(",") if s]
loci = ast.get("loci_draft") or []
notes = ast.get("notes") or []
loci_stems = {str(x.get("stem") or "") for x in loci}
hits = [s for s in expected if s in loci_stems or any(s in str(x.get("anchor") or "") for x in loci)]
plan_reject = len(re.findall(r"plan rechazado en explore_a", log))
tool_reject = len(re.findall(r"tool ignorado en explore_a", log))
a_judge_n = len(re.findall(r"L2 ▸ a_judge verdicts=", log))
a_done_ok = "Phase A OK" in log or "explore_a_ok" in log
print("---- Phase A snapshot ----")
print(f"exit={$RC} a_done_ok={int(a_done_ok)} peeks={ast.get('peeks_used')} turns={ast.get('turns')} expansions={ast.get('expansions')}")
print(f"queue={len(ast.get('queue') or [])} cursor={ast.get('cursor')} loci={len(loci)}")
for loc in loci:
    print(f"  locus [{loc.get('role')}] {loc.get('anchor')} stem={loc.get('stem')} — {str(loc.get('why') or '')[:80]}")
print(f"verdicts={len(notes)} plan_rejects={plan_reject} tool_rejects={tool_reject} a_judge_turns={a_judge_n}")
print(f"expected={expected} hits={hits}")
print(f"artifacts: {case_dir}")
PY

echo "done rc=$RC → $CASE_DIR" | tee -a "$OUT/STARTED.txt"
exit "$RC"
