#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

allowed="src/ui/ui_event_dispatcher.cpp"
fail=0

check_pattern() {
  local pattern="$1"
  local desc="$2"
  while IFS= read -r file; do
    if [[ "$file" == "$allowed" ]]; then
      continue
    fi
    echo "ERROR: $desc found in $file" >&2
    grep -n "$pattern" "$file" >&2 || true
    fail=1
  done < <(grep -rl "$pattern" src || true)
}

check_pattern 'request_ui_tick' 'legacy request_ui_tick'
check_pattern 'schedule_ui_tick' 'legacy schedule_ui_tick'
check_pattern 'force_immediate_repaint' 'legacy force_immediate_repaint'
check_pattern 'PostEvent(Event::Custom)' 'direct PostEvent(Custom)'

if [[ "$fail" -ne 0 ]]; then
  echo "UI wake guard failed." >&2
  exit 1
fi

echo "UI wake guard OK."
