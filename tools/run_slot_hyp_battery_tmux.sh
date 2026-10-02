#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="$ROOT/.tuide/ai/l2_explore_battery/round_zone_judge_slot_hyp_v1_tmux.log"
SOCKET=tide_slot
SESSION=slot_hyp_v1

mkdir -p "$ROOT/.tuide/ai/l2_explore_battery"
pkill -f 'l2_zone_judge_battery.py.*slot_hyp' 2>/dev/null || true
pkill -f 'l2_harness_cli zone-judge' 2>/dev/null || true
pkill -f 'llama-server.*18766' 2>/dev/null || true
sleep 1
tmux -L "$SOCKET" kill-session -t "$SESSION" 2>/dev/null || true
rm -rf "$ROOT/.tuide/ai/l2_explore_battery/round_zone_judge_slot_hyp_v1"
: > "$LOG"

tmux -L "$SOCKET" new-session -d -s "$SESSION" \
  "cd '$ROOT' && python3 tools/l2_zone_judge_battery.py \
    --label zone_judge_slot_hyp_v1 \
    --cards-label zone_judge_recall_v1 \
    --skip-cards --slot-survey \
    2>&1 | tee '$LOG'; echo EXIT=\$? | tee -a '$LOG'; exec bash"

sleep 2
tmux -L "$SOCKET" list-sessions
echo "LOG=$LOG"
echo "Attach: tmux -L $SOCKET attach -t $SESSION"
head -20 "$LOG" || true
pgrep -af 'l2_zone_judge_battery|l2_harness_cli' || true
