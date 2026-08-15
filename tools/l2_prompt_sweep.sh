#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
chmod +x tools/l2_prompt_sweep.py 2>/dev/null || true
exec python3 "$ROOT/tools/l2_prompt_sweep.py" "$@"
