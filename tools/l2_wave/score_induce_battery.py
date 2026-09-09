#!/usr/bin/env python3
"""Puntuación de una ronda wave-explore --control --induce (existence vs vacío)."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def _load(path: Path):
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def _close_kind(why: str) -> str:
    low = (why or "").lower()
    if not low.strip():
        return "none"
    refute = (
        "no halló",
        "no hay camino",
        "sin camino",
        "no cubre",
        "no une",
        "no está",
        "no se encontró",
        "no hay objeto",
        "no llama",
    )
    confirm = ("encontré", "se apaga", "se enciende", "vive en", "se cancela en")
    if any(k in low for k in refute):
        return "refute"
    if any(k in low for k in confirm):
        return "confirm"
    return "other"


def score_run(run_dir: Path) -> dict:
    control = _load(run_dir / "control.json") or {}
    metrics = _load(run_dir / "metrics.json") or {}
    turns = control.get("turns") or []
    jobs = control.get("jobs") or []
    err_plantilla = 0
    err_mapeo = 0
    err_evita = 0
    reopen = 0
    last_q = ""
    for t in turns:
        err = t.get("error") or ""
        if "plantilla" in err:
            err_plantilla += 1
        if "mapeo" in err:
            err_mapeo += 1
        if "evita" in err:
            err_evita += 1
        q = t.get("consulta") or ""
        if q and q == last_q and t.get("do") in ("explorar", "revisar"):
            reopen += 1
        if q:
            last_q = q
    empty_jobs = 0
    for j in jobs:
        visto = j.get("visto") or []
        if not visto:
            empty_jobs += 1
    why = control.get("why") or metrics.get("why") or ""
    return {
        "dir": str(run_dir),
        "induce": control.get("induce") or metrics.get("induce") or "",
        "cerró": bool(control.get("cerró") or metrics.get("cerró")),
        "jobs_run": int(control.get("jobs_run") or metrics.get("jobs_run") or 0),
        "empty_jobs": empty_jobs,
        "plantilla_n": err_plantilla,
        "mapeo_n": err_mapeo,
        "evita_n": err_evita,
        "reopen_n": reopen,
        "amplió": bool(control.get("amplió")),
        "close_kind": _close_kind(why),
        "why": why,
    }


def ok_for_kind(row: dict, kind: str) -> bool:
    if not row.get("cerró"):
        return False
    if kind == "existe":
        return row.get("jobs_run", 0) >= 1 and row.get("empty_jobs", 0) == 0
    if kind == "vacio":
        return row.get("empty_jobs", 0) >= 1 or row.get("close_kind") == "refute"
    return False


def scoreboard(round_dir: Path, manifest: dict) -> dict:
    rows = []
    for induce in manifest["induces"]:
        for case in manifest["cases"]:
            run = round_dir / f"{induce['id']}__{case['id']}"
            row = score_run(run) if (run / "control.json").is_file() else {
                "dir": str(run),
                "induce": induce["id"],
                "cerró": False,
                "jobs_run": 0,
                "empty_jobs": 0,
                "plantilla_n": 0,
                "mapeo_n": 0,
                "evita_n": 0,
                "reopen_n": 0,
                "amplió": False,
                "close_kind": "none",
                "why": "sin control.json",
            }
            row["induce"] = induce["id"]
            row["case"] = case["id"]
            row["kind"] = case["kind"]
            row["ok"] = ok_for_kind(row, case["kind"])
            rows.append(row)
    by_induce = {}
    for row in rows:
        by_induce.setdefault(row["induce"], []).append(row)
    ranking = []
    for induce, items in by_induce.items():
        kinds_ok = {i["kind"] for i in items if i["ok"]}
        ranking.append({
            "induce": induce,
            "existe_ok": "existe" in kinds_ok,
            "vacio_ok": "vacio" in kinds_ok,
            "ambos": kinds_ok >= {"existe", "vacio"},
            "mapeo_n": sum(i["mapeo_n"] for i in items),
            "reopen_n": sum(i["reopen_n"] for i in items),
            "cerró_n": sum(1 for i in items if i["cerró"]),
        })
    ranking.sort(key=lambda r: (
        not r["ambos"],
        not r["vacio_ok"],
        not r["existe_ok"],
        r["mapeo_n"] + r["reopen_n"],
        -r["cerró_n"],
    ))
    return {"rows": rows, "ranking": ranking}


def main() -> int:
    if len(sys.argv) < 2:
        print("score_induce_battery.py ROUND_DIR [manifest.json]", file=sys.stderr)
        return 2
    round_dir = Path(sys.argv[1])
    if len(sys.argv) >= 3:
        man_path = Path(sys.argv[2])
    else:
        man_path = Path(__file__).resolve().parents[2] / "tests/fixtures/l2_wave/induce_battery.json"
    manifest = json.loads(man_path.read_text(encoding="utf-8"))
    board = scoreboard(round_dir, manifest)
    out = round_dir / "scoreboard.json"
    out.write_text(json.dumps(board, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(board["ranking"], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
