#!/usr/bin/env bash
# H_LITE: sense core5 con piloto+explorador estilo Cursor pero tools = Grep+Read.
# La caza la ejecuta el subagente l2-wave-lite-battery (no l2_harness).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
BASE="${1:-$ROOT/.tuide/ai/l2_wave/v2_night/H_LITE}"
REP="${2:-rep_1}"
OUT="$BASE/$REP"
MANIFEST="$ROOT/tests/fixtures/l2_wave/sense_battery.json"
SCORE="$ROOT/tools/l2_wave/score_sense_battery.py"

mkdir -p "$OUT"
echo "H_LITE out=$OUT"
echo "Lanza el subagente Cursor 'l2-wave-lite-battery' (solo Grep+Read)."
echo "Cuando termine:"
echo "  python3 $SCORE $OUT $MANIFEST"

if [[ -f "$OUT/scoreboard.json" ]]; then
  python3 "$SCORE" "$OUT" "$MANIFEST" || true
  python3 - <<PY
import json
from pathlib import Path
sb = json.loads(Path("$OUT/scoreboard.json").read_text())
rows = sb.get("rows") or sb.get("cases") or []
if isinstance(sb.get("ok_frac"), (int, float)):
    ok, og = sb["ok_frac"], sb.get("ok_gold_frac")
else:
    oks = [1.0 if r.get("ok") else 0.0 for r in rows]
    ogs = [1.0 if r.get("ok_gold") else 0.0 for r in rows]
    ok = sum(oks)/len(oks) if oks else 0.0
    og = sum(ogs)/len(ogs) if ogs else 0.0
summary = {
    "label": "H_LITE",
    "tools": ["Grep", "Read"],
    "ok_frac": ok,
    "ok_gold_frac": og,
    "vs": {"H_CURSOR_ok": 0.80, "H_CURSOR_ok_gold": 0.60, "S0_ok": 0.44, "H_T0_ok": 0.40},
    "rows": [{"case": r.get("case"), "ok": r.get("ok"), "ok_gold": r.get("ok_gold"), "kind": r.get("kind")} for r in rows],
}
Path("$BASE/H_LITE_SUMMARY.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n")
print(json.dumps(summary, indent=2, ensure_ascii=False))
PY
fi
