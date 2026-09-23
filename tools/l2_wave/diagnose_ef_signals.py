#!/usr/bin/env python3
"""Diagnóstico post-S0: señales para E (verificar) / F (pistas léxicas) sobre reps v2_night."""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

DUMP_RE = re.compile(r"(?m)^Cerrado:\s*\n\s*leído:\s*", re.I)
VEREDICTO_RE = re.compile(r'"veredicto"\s*:\s*"(hay|no_hay|no_concluyente)"', re.I)
REJECT_RE = re.compile(r"reject|rechaz|ilegal|control: ", re.I)


def analyze_case_dir(case_dir: Path) -> dict:
    jobs = case_dir / "jobs.md"
    log = case_dir / "log.json"
    control = case_dir / "control.json"
    out: dict = {"case": case_dir.name, "path": str(case_dir)}
    text = jobs.read_text(encoding="utf-8", errors="replace") if jobs.is_file() else ""
    out["dump_cerrado"] = bool(DUMP_RE.search(text))
    out["has_veredicto_json"] = bool(VEREDICTO_RE.search(text))
    out["cerrado_n"] = len(re.findall(r"(?m)^Cerrado:", text))
    # consulta verbs in control turns
    consultas = []
    for p in sorted(case_dir.glob("control_*/ola.json")) + sorted(case_dir.glob("control_*/user.md")):
        try:
            raw = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if p.suffix == ".json":
            try:
                data = json.loads(raw)
                if isinstance(data, dict) and data.get("consulta"):
                    consultas.append(str(data["consulta"]))
            except json.JSONDecodeError:
                pass
        else:
            m = re.search(r'"consulta"\s*:\s*"([^"]+)"', raw)
            if m:
                consultas.append(m.group(1))
    out["consultas_n"] = len(consultas)
    # crude same-verb repeat: first token overlap
    stems = []
    for c in consultas:
        toks = re.findall(r"[a-záéíóúñ]{4,}", c.lower())
        stems.append(toks[0] if toks else "")
    repeats = 0
    for i in range(1, len(stems)):
        if stems[i] and stems[i] == stems[i - 1]:
            repeats += 1
    out["same_verb_streak"] = repeats
    out["consultas"] = consultas[:8]
    if control.is_file():
        try:
            cj = json.loads(control.read_text(encoding="utf-8"))
            out["final_do"] = cj.get("do")
            out["final_why"] = (cj.get("why") or "")[:120]
        except json.JSONDecodeError:
            pass
    return out


def analyze_base(base: Path) -> dict:
    rows = []
    for rep in sorted(base.glob("rep_*")):
        for case in sorted(rep.glob("*")):
            if not case.is_dir() or case.name.startswith("."):
                continue
            if not (case / "jobs.md").is_file() and not (case / "control.json").is_file():
                continue
            row = analyze_case_dir(case)
            row["rep"] = rep.name
            rows.append(row)
    n = len(rows) or 1
    dump_n = sum(1 for r in rows if r.get("dump_cerrado"))
    no_ver = sum(1 for r in rows if r.get("cerrado_n") and not r.get("has_veredicto_json"))
    verb_rep = sum(1 for r in rows if (r.get("same_verb_streak") or 0) > 0)
    by_case = defaultdict(list)
    for r in rows:
        by_case[r["case"]].append(r)
    return {
        "base": str(base),
        "cases_n": len(rows),
        "dump_frac": dump_n / n,
        "cerrado_sin_veredicto_frac": no_ver / n,
        "same_verb_repeat_frac": verb_rep / n,
        "recommend_E": dump_n / n >= 0.25 or no_ver / n >= 0.25,
        "recommend_F": verb_rep / n >= 0.2,
        "by_case_dump": {
            c: sum(1 for r in rs if r.get("dump_cerrado")) / max(1, len(rs))
            for c, rs in sorted(by_case.items())
        },
        "by_case_verb_repeat": {
            c: sum(1 for r in rs if (r.get("same_verb_streak") or 0) > 0) / max(1, len(rs))
            for c, rs in sorted(by_case.items())
        },
        "sample": rows[:12],
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("bases", nargs="+", type=Path)
    ap.add_argument("-o", type=Path, required=True)
    args = ap.parse_args()
    out = {"runs": [analyze_base(b) for b in args.bases]}
    # aggregate recommendation
    rec_e = any(r["recommend_E"] for r in out["runs"])
    rec_f = any(r["recommend_F"] for r in out["runs"])
    out["verdict"] = {
        "E_verificar": "candidate" if rec_e else "not_indicated_yet",
        "F_lexical_menu": "candidate" if rec_f else "not_indicated_yet",
        "note": "Basado en dumps/veredicto ausente y repetición de verbo en consultas; no sustituye N≥5 T2.",
    }
    args.o.parent.mkdir(parents=True, exist_ok=True)
    args.o.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(out["verdict"], ensure_ascii=False, indent=2))
    for r in out["runs"]:
        print(
            f"{r['base']}: dump={r['dump_frac']:.2f} no_ver={r['cerrado_sin_veredicto_frac']:.2f} "
            f"verb_rep={r['same_verb_repeat_frac']:.2f} n={r['cases_n']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
