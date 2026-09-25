#!/usr/bin/env python3
"""Measure verifier on trap + easy exists subset."""
from __future__ import annotations

import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBE = Path(__file__).resolve().parent / "probe_admin_pilot.py"
CASES = Path(__file__).resolve().parent / "admin_explore_battery_cases.json"


def main() -> None:
    ids = ["04_deseo_console_gutter", "09_multipolo_console_and_style", "13_deseo_underline_to_gutter",
           "19_deseo_build_panel_redline", "01_short_exists_adminstate", "10_short_exists_diagnostics_struct"]
    if len(sys.argv) > 1:
        ids = [x for x in sys.argv[1].split(",") if x.strip()]
    all_cases = {c["id"]: c for c in json.loads(CASES.read_text())["cases"]}
    out_root = ROOT / ".tuide/ai/l2_admin_probe" / f"verify_measure_{time.strftime('%Y%m%d_%H%M%S')}"
    out_root.mkdir(parents=True, exist_ok=True)
    rows = []
    for cid in ids:
        case = all_cases[cid]
        cdir = out_root / cid
        cdir.mkdir(parents=True, exist_ok=True)
        print(f"==== {cid} ====", flush=True)
        t0 = time.time()
        with (cdir / "run.log").open("w") as logf:
            proc = subprocess.run(
                [
                    sys.executable, "-u", str(PROBE),
                    "--out", str(cdir),
                    "--prompt", case["prompt"],
                    "--max-turns", "24",
                    "--max-explore", "12",
                ],
                cwd=str(ROOT),
                stdout=logf,
                stderr=subprocess.STDOUT,
                timeout=3 * 3600,
                check=False,
            )
        summary = None
        if (cdir / "SUMMARY.json").is_file():
            summary = json.loads((cdir / "SUMMARY.json").read_text())
        reports = (summary or {}).get("verify_reports") or []
        row = {
            "id": cid,
            "kind": case.get("kind"),
            "exit_code": proc.returncode,
            "wall": round(time.time() - t0, 1),
            "exit_do": ((summary or {}).get("closed") or {}).get("do"),
            "n_explore": len([j for j in ((summary or {}).get("jobs") or []) if j.get("tipo") == "explore"]),
            "verify": [
                {"trigger": r.get("trigger"), "veredicto": r.get("veredicto"), "why": (r.get("why") or "")[:200]}
                for r in reports
            ],
            "blocked_any": any(
                (r.get("veredicto") or "") in ("refuta", "dudoso") for r in reports
            ),
            "last_verify": (reports[-1].get("veredicto") if reports else None),
        }
        rows.append(row)
        print(json.dumps(row, ensure_ascii=False), flush=True)
        (cdir / "DONE").write_text(json.dumps(row, ensure_ascii=False, indent=2), encoding="utf-8")

    board = {"out": str(out_root), "rows": rows}
    (out_root / "MEASURE.json").write_text(json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["# Verify measure", ""]
    for r in rows:
        lines.append(
            f"- {r['id']}: last_verify={r['last_verify']} blocked={r['blocked_any']} "
            f"exit={r['exit_do']} explores={r['n_explore']} wall={r['wall']}"
        )
    (out_root / "MEASURE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("DONE", out_root, flush=True)


if __name__ == "__main__":
    main()
