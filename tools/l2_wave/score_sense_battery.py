#!/usr/bin/env python3
"""Puntuación de una ronda sense12 (existe / hueco / absurdo)."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def _load(path: Path):
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


REFUTE_KEYS = (
    "no halló",
    "no hay camino",
    "sin camino",
    "no cubre",
    "no une",
    "no está",
    "no se encontró",
    "no hay objeto",
    "no llama",
    "no existe",
    "se refuta",
    "refut",
    "no hay captura",
    "no hay módulo",
    "invent",
    "no vive en este",
    "nada de eso",
)
CONFIRM_KEYS = (
    "encontré",
    "se apaga",
    "se enciende",
    "vive en",
    "se cancela en",
    "circuito está completo",
    "el mecanismo",
)
HUMO_KEYS = (
    "kubernetes",
    "balíst",
    "balist",
    "cuántic",
    "cuantic",
    "parpade",
)


def _close_kind(why: str) -> str:
    low = (why or "").lower()
    if not low.strip():
        return "none"
    if any(k in low for k in REFUTE_KEYS):
        return "refute"
    if any(k in low for k in CONFIRM_KEYS):
        return "confirm"
    return "other"


def _humo(why: str, kind: str) -> bool:
    if kind != "absurdo":
        return False
    low = (why or "").lower()
    if not low.strip():
        return False
    if _close_kind(why) == "refute":
        return False
    return any(k in low for k in HUMO_KEYS) and _close_kind(why) == "confirm"


def score_run(run_dir: Path) -> dict:
    control = _load(run_dir / "control.json") or {}
    metrics = _load(run_dir / "metrics.json") or {}
    turns = control.get("turns") or []
    jobs = control.get("jobs") or []
    err_plantilla = err_mapeo = err_evita = reopen = 0
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
    empty_jobs = sum(1 for j in jobs if not (j.get("visto") or []))
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
        "humo": False,
        "why": why,
    }


def ok_for_kind(row: dict, kind: str) -> bool:
    if not row.get("cerró"):
        return False
    if kind == "existe":
        return row.get("jobs_run", 0) >= 1 and row.get("empty_jobs", 0) == 0
    if kind == "hueco":
        return row.get("empty_jobs", 0) >= 1 or row.get("close_kind") == "refute"
    if kind == "absurdo":
        return row.get("close_kind") == "refute" and not row.get("humo")
    return False


def _blob_for_gold(run_dir: Path, row: dict) -> str:
    parts = [row.get("why") or ""]
    control = _load(run_dir / "control.json") or {}
    for j in control.get("jobs") or []:
        for v in j.get("visto") or []:
            parts.append(str(v))
        parts.append(str(j.get("consulta") or ""))
    jobs_md = run_dir / "jobs.md"
    if jobs_md.is_file():
        parts.append(jobs_md.read_text(encoding="utf-8", errors="replace"))
    return "\n".join(parts).lower()


def gold_check(run_dir: Path, row: dict, gold: dict | None) -> dict:
    """A3: comprobación estructurada opcional. No sustituye ok_for_kind todavía;
    aporta gold_ok + detalle para el scoreboard."""
    if not gold:
        return {"gold_ok": None, "hit_symbols": [], "forbid_hit": [], "notes": "sin gold"}
    blob = _blob_for_gold(run_dir, row)
    symbols = [s.lower() for s in (gold.get("symbols_any") or [])]
    hit = [s for s in symbols if s in blob]
    forbid = [s.lower() for s in (gold.get("forbid_why") or [])]
    forbid_hit = [s for s in forbid if s in blob]
    kind = row.get("kind")
    gold_ok = True
    notes = []
    if kind == "existe" and symbols and not hit:
        gold_ok = False
        notes.append("falta symbols_any")
    if kind == "existe" and row.get("close_kind") == "refute" and not hit:
        gold_ok = False
        notes.append("existe refutado sin símbolo")
    if kind == "hueco" and forbid_hit:
        # sella el falso amigo
        gold_ok = False
        notes.append("forbid_why en cierre")
    if kind == "absurdo":
        if row.get("close_kind") != "refute" or row.get("humo"):
            gold_ok = False
            notes.append("absurdo no refuta limpio")
        if forbid_hit and row.get("close_kind") == "confirm":
            gold_ok = False
    if not row.get("cerró"):
        gold_ok = False
        notes.append("no cerró")
    return {
        "gold_ok": gold_ok,
        "hit_symbols": hit,
        "forbid_hit": forbid_hit,
        "notes": "; ".join(notes) if notes else "ok",
    }


def scoreboard(round_dir: Path, manifest: dict) -> dict:
    rows = []
    for case in manifest["cases"]:
        run = round_dir / case["id"]
        row = (
            score_run(run)
            if (run / "control.json").is_file()
            else {
                "dir": str(run),
                "induce": "",
                "cerró": False,
                "jobs_run": 0,
                "empty_jobs": 0,
                "plantilla_n": 0,
                "mapeo_n": 0,
                "evita_n": 0,
                "reopen_n": 0,
                "amplió": False,
                "close_kind": "none",
                "humo": False,
                "why": "sin control.json",
            }
        )
        row["case"] = case["id"]
        row["kind"] = case["kind"]
        row["humo"] = _humo(row.get("why") or "", case["kind"])
        row["ok"] = ok_for_kind(row, case["kind"])
        g = gold_check(run, row, case.get("gold"))
        row["gold_ok"] = g["gold_ok"]
        row["gold_hit_symbols"] = g["hit_symbols"]
        row["gold_forbid_hit"] = g["forbid_hit"]
        row["gold_notes"] = g["notes"]
        # Criterio combinado para A3: si hay gold, exige ok laxo AND gold_ok
        if g["gold_ok"] is not None:
            row["ok_gold"] = bool(row["ok"] and g["gold_ok"])
        else:
            row["ok_gold"] = row["ok"]
        rows.append(row)
    kinds_ok = {r["kind"] for r in rows if r["ok"]}
    kinds_gold = {r["kind"] for r in rows if r.get("ok_gold")}
    return {
        "rows": rows,
        "summary": {
            "existe_ok": "existe" in kinds_ok,
            "hueco_ok": "hueco" in kinds_ok,
            "absurdo_ok": "absurdo" in kinds_ok,
            "todos": kinds_ok >= {"existe", "hueco", "absurdo"},
            "cerró_n": sum(1 for r in rows if r["cerró"]),
            "mapeo_n": sum(r["mapeo_n"] for r in rows),
            "reopen_n": sum(r["reopen_n"] for r in rows),
            "humo_n": sum(1 for r in rows if r.get("humo")),
            "gold_ok_n": sum(1 for r in rows if r.get("gold_ok")),
            "ok_gold_n": sum(1 for r in rows if r.get("ok_gold")),
            "todos_gold": kinds_gold >= {"existe", "hueco", "absurdo"},
        },
    }


def main() -> int:
    if len(sys.argv) < 2:
        print("score_sense_battery.py ROUND_DIR [manifest.json]", file=sys.stderr)
        return 2
    round_dir = Path(sys.argv[1])
    man_path = (
        Path(sys.argv[2])
        if len(sys.argv) >= 3
        else Path(__file__).resolve().parents[2] / "tests/fixtures/l2_wave/sense_battery.json"
    )
    manifest = json.loads(man_path.read_text(encoding="utf-8"))
    board = scoreboard(round_dir, manifest)
    (round_dir / "scoreboard.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(board["summary"], ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
