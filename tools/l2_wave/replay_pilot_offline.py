#!/usr/bin/env python3
"""A6 esqueleto: replay offline — valida JSON de ola.json grabados vs esquema mínimo."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def check_ola(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError) as e:
        return {"path": str(path), "ok": False, "error": str(e)}
    if not isinstance(data, dict):
        return {"path": str(path), "ok": False, "error": "not object"}
    # Grabados a veces son solo el objeto control_v1; exigimos do.
    missing = []
    if "do" not in data:
        missing.append("do")
    return {
        "path": str(path),
        "ok": not missing,
        "do": data.get("do"),
        "action": data.get("action"),
        "missing": missing,
        "has_why": "why" in data,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument(
        "corpus",
        nargs="?",
        default=str(root / "tests/fixtures/l2_wave/corpus"),
    )
    ap.add_argument("-o", "--out", type=Path)
    args = ap.parse_args()
    corpus = Path(args.corpus)
    rows = []
    for ola in sorted(corpus.glob("*/ola.json")):
        rows.append(check_ola(ola))
    summary = {
        "n": len(rows),
        "ok_n": sum(1 for r in rows if r["ok"]),
        "by_do": {},
    }
    for r in rows:
        if r.get("do"):
            summary["by_do"][r["do"]] = summary["by_do"].get(r["do"], 0) + 1
    out = {"summary": summary, "rows": rows[:50], "rows_truncated": len(rows) > 50}
    text = json.dumps(out, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["n"] == 0 or summary["ok_n"] == summary["n"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
