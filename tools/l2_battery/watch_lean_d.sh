#!/usr/bin/env bash
# Smoke sibling → hard after anti-loop + n_ctx reserve + tighter edit GBNF.
# Single flock instance.
set -euo pipefail
cd /home/lariasr/workspace/TIDE
LOCK=/tmp/l2_phase_lean_d.lock
exec 9>"$LOCK"
if ! flock -n 9; then
  echo "$(date -Iseconds) lean D watcher already running (lock $LOCK); abort" >&2
  exit 1
fi

LOG=.tuide/ai/l2_phase_lean_d/watch.log
mkdir -p .tuide/ai/l2_phase_lean_d .tuide/ai/l2_phase_lean_d_smoke .tuide/ai/l2_phase_lean_d_hard
echo "$(date -Iseconds) lean D (anti-loop + grammar-tight + n_ctx) smoke start" | tee -a "$LOG"

python3 tools/l2_battery/kill_l2_runtime.py | tee -a "$LOG"

export L2_PROMPT_PACK="$PWD/tools/l2_battery/prompt_packs/t_sibling_guide.json"
export L2_FEAT_POST_EDIT_COVERAGE=1
export L2_FEAT_EDIT_LEAN_PROMPT=1
export L2_FEAT_JSON_GRAMMAR=1
git checkout -- src/ui src/util 2>/dev/null || true
python3 tools/l2_battery/run_closed_loop_round.py \
  --cases tools/l2_battery/prompt_packs/cases_sibling_only.json \
  --out .tuide/ai/l2_phase_lean_d_smoke \
  --cli build/l2_harness_cli \
  --root "$PWD" \
  --case-timeout 2700 \
  > .tuide/ai/l2_phase_lean_d_smoke/console.log 2>&1
echo "$(date -Iseconds) lean D smoke done" | tee -a "$LOG"
python3 tools/l2_battery/score_facets.py \
  --cases tools/l2_battery/prompt_packs/cases_sibling_only.json \
  --round-dir .tuide/ai/l2_phase_lean_d_smoke \
  > .tuide/ai/l2_phase_lean_d_smoke/metrics_stdout.json 2>&1 || true
git checkout -- src/ui src/util 2>/dev/null || true
echo "$(date -Iseconds) lean D hard start" | tee -a "$LOG"
python3 tools/l2_battery/run_closed_loop_round.py \
  --cases tools/l2_battery/prompt_packs/cases_hard.json \
  --out .tuide/ai/l2_phase_lean_d_hard \
  --cli build/l2_harness_cli \
  --root "$PWD" \
  --case-timeout 2700 \
  > .tuide/ai/l2_phase_lean_d_hard/console.log 2>&1
echo "$(date -Iseconds) lean D hard done" | tee -a "$LOG"
python3 tools/l2_battery/score_facets.py \
  --cases tools/l2_battery/prompt_packs/cases_hard.json \
  --round-dir .tuide/ai/l2_phase_lean_d_hard \
  > .tuide/ai/l2_phase_lean_d_hard/metrics_stdout.json 2>&1 || true
git checkout -- src/ui src/util 2>/dev/null || true
echo "$(date -Iseconds) lean D all done" | tee -a "$LOG"
