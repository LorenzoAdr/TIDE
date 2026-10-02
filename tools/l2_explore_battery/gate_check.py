#!/usr/bin/env python3
"""Check gates 1B, 2, 3B for core5 anchor-causal batteries."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.score_anchor_graph import gate_2  # noqa: E402
from tools.l2_explore_battery.score_f1_anchor import gate_3b  # noqa: E402
from tools.l2_explore_battery.score_problem_frame import gate_1b  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--round-dir", type=Path, required=True)
    ap.add_argument("--gate", choices=("1b", "2", "3b", "all"), default="all")
    args = ap.parse_args()
    rd = args.round_dir
    rc = 0
    report: dict = {}

    if args.gate in ("1b", "all"):
        p = rd / "problem_frame_score.json"
        if not p.exists():
            print("missing problem_frame_score.json", file=sys.stderr)
            rc = 1
        else:
            data = json.loads(p.read_text(encoding="utf-8"))
            ok, reasons = gate_1b(data.get("summary") or {})
            report["gate_1b"] = {"pass": ok, "reasons": reasons}
            if not ok:
                rc = 1

    if args.gate in ("2", "all"):
        p = rd / "anchor_graph_score.json"
        if not p.exists():
            print("missing anchor_graph_score.json", file=sys.stderr)
            rc = 1
        else:
            data = json.loads(p.read_text(encoding="utf-8"))
            ok, reasons = gate_2(data.get("summary") or {})
            report["gate_2"] = {"pass": ok, "reasons": reasons}
            if not ok:
                rc = 1

    if args.gate in ("3b", "all"):
        p = rd / "f1_anchor_score.json"
        if not p.exists():
            print("missing f1_anchor_score.json", file=sys.stderr)
            rc = 1
        else:
            data = json.loads(p.read_text(encoding="utf-8"))
            ok, reasons = gate_3b(data.get("summary") or {}, data.get("rows") or [])
            report["gate_3b"] = {"pass": ok, "reasons": reasons}
            if not ok:
                rc = 1

    print(json.dumps(report, indent=2))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
