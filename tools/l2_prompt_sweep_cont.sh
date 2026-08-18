#!/usr/bin/env bash
# Continuation prompt-pack sweep: hard cases + untested packs vs p_baseline_cont.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export L2_PROMPT_SWEEP_DIR="${L2_PROMPT_SWEEP_DIR:-$ROOT/.tuide/ai/l2_prompt_sweep_cont2}"
export L2_PROMPT_PLAYBOOK="${L2_PROMPT_PLAYBOOK:-$ROOT/tools/l2_battery/prompt_packs/playbook_cont.json}"
export L2_PROMPT_CASES="${L2_PROMPT_CASES:-$ROOT/tools/l2_battery/prompt_packs/cases_hard.json}"
chmod +x tools/l2_prompt_sweep.py 2>/dev/null || true
exec python3 "$ROOT/tools/l2_prompt_sweep.py" "$@"
