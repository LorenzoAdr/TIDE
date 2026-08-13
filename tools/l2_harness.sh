#!/usr/bin/env bash
# L2 harness helpers (file protocol under .tuide/ai/l2/).
# Tool execution happens inside tuide (/l2_turn|/l2_tool). This script manages
# request.json, shows session status, and validates a finished session.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L2="$ROOT/.tuide/ai/l2"
SESSION="$L2/session.md"
REQUEST="$L2/request.json"
RESPONSE="$L2/response.json"
TRACE="$L2/trace.ndjson"
STATE="$L2/state.json"
MAP="$ROOT/.tuide/ai/map_last.md"

usage() {
  cat <<'EOF'
Usage: tools/l2_harness.sh <cmd> [args]

  status              Show harness paths + state
  bootstrap           Ensure dirs; copy note if map_last exists (prefer /l2_session bootstrap in tuide)
  request tool NAME [ARG…]
                      Write request.json for a tool turn
  request done SUMMARY
                      Write request.json for done
  show-session        Print session.md (or head)
  show-response       Print response.json
  validate [session.md] Check session structure + ≥3 tool turns + done
  validate-fixtures     Validate the 3 canned cases under tests/fixtures/l2_harness/
  cases               Print the 3 validation query cases

After writing request.json, run inside tuide AI tab:
  /l2_turn
EOF
}

cmd="${1:-}"
shift || true

case "$cmd" in
  ""|-h|--help|help)
    usage
    ;;
  status)
    mkdir -p "$L2"
    echo "root=$ROOT"
    echo "l2=$L2"
    echo "map_last=$([ -f "$MAP" ] && echo present || echo missing)"
    echo "session=$([ -f "$SESSION" ] && wc -c <"$SESSION" | tr -d ' ' || echo missing) chars"
    if [[ -f "$STATE" ]]; then
      cat "$STATE"
    else
      echo "state: missing"
    fi
    ;;
  bootstrap)
    mkdir -p "$L2"
    if [[ ! -f "$MAP" ]]; then
      echo "warn: $MAP missing — run an L1 investigate/context query in tuide first" >&2
    fi
    cat >"$REQUEST" <<'EOF'
{
  "action": "tool",
  "name": "get_code_of",
  "arg": ""
}
EOF
    echo "Wrote template $REQUEST"
    echo "In tuide (ai.level2.mode=harness): run a query, then /l2_session status"
    echo "Or: /l2_session bootstrap <query>"
    ;;
  request)
    mkdir -p "$L2"
    sub="${1:-}"
    shift || true
    if [[ "$sub" == "tool" ]]; then
      name="${1:-}"
      shift || true
      arg="${*:-}"
      if [[ -z "$name" ]]; then
        echo "usage: $0 request tool NAME [ARG…]" >&2
        exit 2
      fi
      python3 - "$REQUEST" "$name" "$arg" <<'PY'
import json, sys
path, name, arg = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path, "w", encoding="utf-8") as f:
    json.dump({"action": "tool", "name": name, "arg": arg}, f, indent=2, ensure_ascii=False)
    f.write("\n")
print("wrote", path)
PY
    elif [[ "$sub" == "done" ]]; then
      summary="${*:-}"
      python3 - "$REQUEST" "$summary" <<'PY'
import json, sys
path, summary = sys.argv[1], sys.argv[2]
with open(path, "w", encoding="utf-8") as f:
    json.dump({"action": "done", "summary": summary}, f, indent=2, ensure_ascii=False)
    f.write("\n")
print("wrote", path)
PY
    else
      echo "usage: $0 request tool NAME [ARG…] | $0 request done SUMMARY" >&2
      exit 2
    fi
    ;;
  show-session)
    if [[ ! -f "$SESSION" ]]; then
      echo "missing $SESSION" >&2
      exit 1
    fi
    if [[ "${1:-}" == "head" ]]; then
      head -n 80 "$SESSION"
    else
      cat "$SESSION"
    fi
    ;;
  show-response)
    if [[ ! -f "$RESPONSE" ]]; then
      echo "missing $RESPONSE" >&2
      exit 1
    fi
    cat "$RESPONSE"
    ;;
  validate)
    SESSION_PATH="${1:-$SESSION}"
    fail=0
    if [[ ! -f "$SESSION_PATH" ]]; then
      echo "FAIL: missing session.md ($SESSION_PATH)" >&2
      exit 1
    fi
    for mark in "## Tool guide" "## Instruction" "## Ranked map" "## Observations"; do
      if ! grep -qF "$mark" "$SESSION_PATH"; then
        echo "FAIL: missing section $mark" >&2
        fail=1
      fi
    done
    turns=$(grep -c '^### turn ' "$SESSION_PATH" || true)
    tool_turns=$(grep -c '^### turn .* — `' "$SESSION_PATH" || true)
    done_turns=$(grep -c '^### turn .* — done' "$SESSION_PATH" || true)
    echo "session=$SESSION_PATH"
    echo "turns=$turns toolish=$tool_turns done=$done_turns"
    if [[ "$tool_turns" -lt 3 ]]; then
      echo "FAIL: need ≥3 tool observations (got $tool_turns)" >&2
      fail=1
    fi
    if [[ "$done_turns" -lt 1 ]]; then
      echo "FAIL: missing done turn" >&2
      fail=1
    fi
    if [[ "$fail" -ne 0 ]]; then
      exit 1
    fi
    echo "OK: session validates harness success criteria"
    ;;
  validate-fixtures)
    fail=0
    for c in case_wake case_embed case_map; do
      echo "=== fixture $c ==="
      if ! "$0" validate "$ROOT/tests/fixtures/l2_harness/$c/session.md"; then
        fail=1
      fi
    done
    if [[ "$fail" -ne 0 ]]; then
      exit 1
    fi
    echo "OK: all 3 fixtures"
    ;;
  cases)
    cat <<'EOF'
Validation cases (run L1 in tuide with ai.level2.mode=harness, then Cursor L2 loop):

1) cómo despierta la UI / wake bridge
2) dónde se arma el embed / two-stage L1
3) cómo se escribe map_last

Prompt template: docs/ai/l2-harness-prompt.md
EOF
    ;;
  *)
    echo "unknown cmd: $cmd" >&2
    usage >&2
    exit 2
    ;;
esac
