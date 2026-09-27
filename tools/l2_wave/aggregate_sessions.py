#!/usr/bin/env python3
"""Recompute BASELINE.json from an existing battery out dir (SCOREBOARD rows or case dirs)."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from admin_session_metrics import aggregate_baseline, score_session


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cases", type=Path, default=None)
    args = ap.parse_args()
    out: Path = args.out
    board_path = out / "SCOREBOARD.json"
    cases_by_id: dict = {}
    if args.cases and args.cases.is_file():
        for c in json.loads(args.cases.read_text(encoding="utf-8")).get("cases") or []:
            cases_by_id[c["id"]] = c
    rows = []
    if board_path.is_file():
        rows = list(json.loads(board_path.read_text(encoding="utf-8")).get("rows") or [])
    else:
        for d in sorted(out.iterdir()):
            if not d.is_dir() or not (d / "SUMMARY.json").is_file():
                continue
            case = cases_by_id.get(d.name) or json.loads((d / "case.json").read_text()) if (d / "case.json").is_file() else {"id": d.name}
            summary = json.loads((d / "SUMMARY.json").read_text(encoding="utf-8"))
            session = None
            if (d / "SESSION.json").is_file():
                session = json.loads((d / "SESSION.json").read_text(encoding="utf-8"))
            rows.append(score_session(case=case, summary=summary, session=session, err=None))
    n_planned = len(cases_by_id) if cases_by_id else len(rows)
    baseline = aggregate_baseline(rows, n_planned=n_planned)
    (out / "BASELINE.json").write_text(json.dumps(baseline, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps({k: baseline[k] for k in ("n_scored", "process_ok_frac", "honest_frac", "lie_frac", "terminal_mix")}, ensure_ascii=False))


if __name__ == "__main__":
    main()
