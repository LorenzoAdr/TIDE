#!/usr/bin/env bash
# Smoke sibling → hard with A+B+D+C (JSON GBNF). Single flock instance.
set -euo pipefail
cd /home/lariasr/workspace/TIDE
LOCK=/tmp/l2_phase_lean_c.lock
exec 9>"$LOCK"
if ! flock -n 9; then
  echo "$(date -Iseconds) lean C watcher already running (lock $LOCK); abort" >&2
  exit 1
fi

LOG=.tuide/ai/l2_phase_lean_c/watch.log
mkdir -p .tuide/ai/l2_phase_lean_c .tuide/ai/l2_phase_lean_c_smoke .tuide/ai/l2_phase_lean_c_hard
echo "$(date -Iseconds) lean C (A+B+D+grammar) smoke start" | tee -a "$LOG"

python3 tools/l2_battery/kill_l2_runtime.py | tee -a "$LOG"

export L2_PROMPT_PACK="$PWD/tools/l2_battery/prompt_packs/t_sibling_guide.json"
export L2_FEAT_POST_EDIT_COVERAGE=1
export L2_FEAT_EDIT_LEAN_PROMPT=1
export L2_FEAT_JSON_GRAMMAR=1
git checkout -- src/ui src/util 2>/dev/null || true
python3 tools/l2_battery/run_closed_loop_round.py \
  --cases tools/l2_battery/prompt_packs/cases_sibling_only.json \
  --out .tuide/ai/l2_phase_lean_c_smoke \
  --cli build/l2_harness_cli \
  --root "$PWD" \
  --case-timeout 2700 \
  > .tuide/ai/l2_phase_lean_c_smoke/console.log 2>&1
echo "$(date -Iseconds) lean C smoke done" | tee -a "$LOG"
python3 tools/l2_battery/score_facets.py \
  --cases tools/l2_battery/prompt_packs/cases_sibling_only.json \
  --round-dir .tuide/ai/l2_phase_lean_c_smoke \
  > .tuide/ai/l2_phase_lean_c_smoke/metrics_stdout.json 2>&1 || true
git checkout -- src/ui src/util 2>/dev/null || true
echo "$(date -Iseconds) lean C hard start" | tee -a "$LOG"
python3 tools/l2_battery/run_closed_loop_round.py \
  --cases tools/l2_battery/prompt_packs/cases_hard.json \
  --out .tuide/ai/l2_phase_lean_c_hard \
  --cli build/l2_harness_cli \
  --root "$PWD" \
  --case-timeout 2700 \
  > .tuide/ai/l2_phase_lean_c_hard/console.log 2>&1
echo "$(date -Iseconds) lean C hard done" | tee -a "$LOG"
python3 tools/l2_battery/score_facets.py \
  --cases tools/l2_battery/prompt_packs/cases_hard.json \
  --round-dir .tuide/ai/l2_phase_lean_c_hard \
  > .tuide/ai/l2_phase_lean_c_hard/metrics_stdout.json 2>&1 || true
git checkout -- src/ui src/util 2>/dev/null || true
echo "$(date -Iseconds) lean C all done" | tee -a "$LOG"
