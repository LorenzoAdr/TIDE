#!/usr/bin/env bash
# Stem-embed passage-profile battery.
# Usage: ./tools/stem_embed_battery_run.sh <profile|all> [--passages-only]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PROFILE="${1:?profile name or all}"
shift || true
CLI="$ROOT/build/stem_embed_battery"
QRELS="$ROOT/tests/fixtures/stem_embed_battery/qrels.json"
BAT="$ROOT/.tuide/ai/stem_embed_battery"
mkdir -p "$BAT"

if [[ ! -x "$CLI" ]]; then
  echo "building stem_embed_battery…"
  cmake --build "$ROOT/build" --target stem_embed_battery -j"$(nproc)"
fi
test -x "$CLI"

PROFILES=(baseline type_first sig_snip hdr_doc module_blurb rich_480 rich_720 kitchen_sink)

run_one() {
  local p="$1"
  shift
  local out="$BAT/round_$p"
  mkdir -p "$out"
  echo "==== profile=$p $(date -Iseconds) ====" | tee "$out/STARTED.txt"
  "$CLI" --profile "$p" --workspace "$ROOT" --qrels "$QRELS" --out "$out" "$@" | tee "$out/console.log"
  echo "wrote $out/summary.json"
}

if [[ "$PROFILE" == "all" ]]; then
  for p in "${PROFILES[@]}"; do
    run_one "$p" "$@"
  done
  python3 - <<'PY'
import json
from pathlib import Path
bat = Path(".tuide/ai/stem_embed_battery")
rows = []
for p in bat.glob("round_*/summary.json"):
    s = json.loads(p.read_text())
    rows.append(s)
rows.sort(key=lambda s: (s.get("mrr") is None, -(s.get("mrr") or 0), s.get("passage_chars_total") or 0))
cmp_path = bat / "compare.json"
cmp_path.write_text(json.dumps(rows, indent=2) + "\n")
print("\n=== compare (by MRR desc) ===")
for s in rows:
    mrr = s.get("mrr")
    h3 = s.get("hit_at_3")
    chars = s.get("passage_chars_total")
    print(f"{s.get('profile'):14} mrr={mrr} hit@3={h3} chars_total={chars}")
print("wrote", cmp_path)
PY
else
  run_one "$PROFILE" "$@"
fi
