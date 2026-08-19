#!/usr/bin/env bash
# Batería NL humana (10 prompts) — ranking L1 offline: léxico + stem boost.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
LABEL="${1:-nl_human}"
CLI="$ROOT/build/stem_boost_battery"
PROMPTS="$ROOT/tests/fixtures/stem_boost_battery/prompts_nl_human.json"
OUT="$ROOT/.tuide/ai/stem_boost_battery/round_${LABEL}"
mkdir -p "$OUT"
if [[ ! -x "$CLI" ]]; then
  cmake --build "$ROOT/build" --target stem_boost_battery -j"$(nproc)"
fi
echo "==== Batería NL humana L1 ($LABEL) $(date -Iseconds) ====" | tee "$OUT/STARTED.txt"
echo "prompts=$PROMPTS cases=10" | tee -a "$OUT/STARTED.txt"
# Liberar el servidor de embeddings de runs anteriores.
fuser -k 18765/tcp >/dev/null 2>&1 || true
"$CLI" --workspace "$ROOT" --prompts "$PROMPTS" --out "$OUT" --label "$LABEL" --verbose \
  | tee "$OUT/console.log"
python3 - <<PY
import json
from pathlib import Path
out = Path("$OUT")
rows = [json.loads(l) for l in (out / "results.jsonl").read_text().splitlines() if l.strip()]
summary = json.loads((out / "summary.json").read_text())
print("\n=== Tabla resumen ===")
print(f"{'id':<32} {'sl':>3} {'map':>3} {'ctx':<22} trap lift")
for r in rows:
    sl = r.get("rank_sl_on") or 0
    mp = r.get("rank_map_on") or 0
    ctx = (r.get("context_stem") or "")[:20]
    trap = "Y" if r.get("trap_above") else "-"
    lift = r.get("lift_sl", 0)
    ok = sl > 0 and sl <= 3 and mp > 0 and mp <= 5 and not r.get("trap_above")
    mark = "OK" if ok else "!!"
    print(f"{r['id']:<32} {sl:>3} {mp:>3} {ctx:<22} {trap} {lift:+4d}  {mark}")
hits_sl1 = sum(1 for r in rows if (r.get("rank_sl_on") or 99) == 1)
hits_sl3 = sum(1 for r in rows if 0 < (r.get("rank_sl_on") or 99) <= 3)
hits_map5 = sum(1 for r in rows if 0 < (r.get("rank_map_on") or 99) <= 5)
traps = sum(1 for r in rows if r.get("trap_above"))
miss = sum(1 for r in rows if (r.get("rank_sl_on") or 0) == 0 or (r.get("rank_map_on") or 0) == 0)
print(f"\nAgregado: hit@1={hits_sl1}/10 hit@3={hits_sl3}/10 map@5={hits_map5}/10 traps={traps} miss={miss}")
print(f"summary.json: shortlist_mrr={summary.get('shortlist_mrr'):.2f} map_mrr={summary.get('map_mrr'):.2f}")
PY
echo "wrote $OUT/summary.json and $OUT/results.jsonl"
