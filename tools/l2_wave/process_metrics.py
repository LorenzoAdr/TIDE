#!/usr/bin/env python3
"""A2: métricas de proceso sobre carpetas sense*/round_* (plan piloto v2)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

DUMP_RE = re.compile(r"(?m)^Cerrado:\s*\n\s*leído:\s*", re.I)
NIEGA_KEYS = (
    "no halló",
    "no hay camino",
    "sin camino",
    "no cubre",
    "no une",
    "no está",
    "no se encontró",
    "no encontr",
    "no hay objeto",
    "no llama",
    "no existe",
    "se refuta",
    "refut",
    "nada de eso",
    "invent",
)
AFIRMA_KEYS = (
    "encontré",
    "se apaga",
    "se enciende",
    "vive en",
    "se cancela en",
    "circuito está completo",
    "el mecanismo",
    "persiste",
    "restaura",
    "reinicia",
)


def _load(path: Path):
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return None


def _cerrado_blocks(jobs_md: str) -> list[str]:
    if not jobs_md:
        return []
    parts = re.split(r"(?m)^Cerrado:\s*$", jobs_md)
    out = []
    for p in parts[1:]:
        block = p.split("### Trabajo")[0].split("\nAbierto:")[0].strip()
        if block:
            out.append(block)
    return out


def _classify_cerrado(block: str) -> str:
    low = block.lower().strip()
    if not low:
        return "vacio"
    if low.startswith("leído:") or low.startswith("leido:"):
        # solo nombres / dump
        rest = low.split(":", 1)[-1].strip()
        if len(rest) < 120 and "\n" not in rest.strip():
            return "dump"
        if re.match(r"^[\w\s,;:/.\-]+$", rest) and not any(k in low for k in NIEGA_KEYS):
            return "dump"
    if any(k in low for k in NIEGA_KEYS):
        return "niega"
    if any(k in low for k in AFIRMA_KEYS):
        return "afirma"
    if low.startswith("leído:") or "leído:" in low[:40]:
        return "dump"
    return "otro"


def _plan_fases(control: dict, plan_path: Path) -> int:
    plan = control.get("plan") or {}
    if isinstance(plan, dict):
        fases = plan.get("fases") or plan.get("phases") or []
        if fases:
            return len(fases)
    pj = _load(plan_path)
    if isinstance(pj, dict):
        fases = pj.get("fases") or pj.get("phases") or []
        return len(fases)
    return 0


def analyze_case(case_dir: Path) -> dict | None:
    control = _load(case_dir / "control.json")
    if not control:
        return None
    turns = control.get("turns") or []
    rejected = [t for t in turns if t.get("error")]
    jobs_md = ""
    jm = case_dir / "jobs.md"
    if jm.is_file():
        jobs_md = jm.read_text(encoding="utf-8", errors="replace")
    blocks = _cerrado_blocks(jobs_md)
    classes = Counter(_classify_cerrado(b) for b in blocks)
    # fallback: jobs in control without jobs.md blocks
    if not blocks and control.get("jobs"):
        for j in control["jobs"]:
            # no cerrado text in json usually
            classes["sin_texto"] += 1
    fases = _plan_fases(control, case_dir / "plan.json")
    jobs_run = int(control.get("jobs_run") or len(control.get("jobs") or []) or 0)
    return {
        "case": case_dir.name,
        "turns": len(turns),
        "rejected_turns": len(rejected),
        "reject_rate": (len(rejected) / len(turns)) if turns else 0.0,
        "reject_reasons": Counter(
            (t.get("error") or "error").split(":")[0][:80] for t in rejected
        ),
        "cerrados": dict(classes),
        "cerrados_n": sum(classes.values()),
        "jobs_run": jobs_run,
        "plan_fases": fases,
        "cerro": bool(control.get("cerró")),
        "why": (control.get("why") or "")[:300],
        "deadlock3": _deadlock_streak(turns) >= 3,
        "max_reject_streak": _deadlock_streak(turns),
    }


def _deadlock_streak(turns: list) -> int:
    best = cur = 0
    for t in turns:
        if t.get("error"):
            cur += 1
            best = max(best, cur)
        else:
            cur = 0
    return best


def iter_rounds(root: Path) -> list[tuple[str, Path]]:
    found = []
    for sense in sorted(root.glob("sense*")):
        if not sense.is_dir():
            continue
        for rd in sorted(sense.glob("round_*")):
            if rd.is_dir():
                found.append((sense.name, rd))
    return found


def analyze_tree(root: Path) -> dict:
    rounds = []
    cerr_tot = Counter()
    reject_n = turn_n = 0
    deadlock_cases = 0
    cases_n = 0
    jobs_vs_plan = []
    score_hits = []

    for sense, rd in iter_rounds(root):
        cases = []
        for child in sorted(rd.iterdir()):
            if not child.is_dir():
                continue
            if child.name.startswith("."):
                continue
            row = analyze_case(child)
            if not row:
                continue
            row["sense"] = sense
            row["round"] = rd.name
            cases.append(row)
            cases_n += 1
            reject_n += row["rejected_turns"]
            turn_n += row["turns"]
            for k, v in row["cerrados"].items():
                cerr_tot[k] += v
            if row["deadlock3"]:
                deadlock_cases += 1
            if row["plan_fases"] > 0:
                jobs_vs_plan.append(
                    {
                        "sense": sense,
                        "round": rd.name,
                        "case": row["case"],
                        "jobs_run": row["jobs_run"],
                        "plan_fases": row["plan_fases"],
                        "ratio": row["jobs_run"] / row["plan_fases"],
                    }
                )
        sb = _load(rd / "scoreboard.json")
        if sb and "rows" in sb:
            oks = sum(1 for r in sb["rows"] if r.get("ok"))
            score_hits.append(
                {
                    "sense": sense,
                    "round": rd.name,
                    "ok_n": oks,
                    "n": len(sb["rows"]),
                    "frac": oks / len(sb["rows"]) if sb["rows"] else 0.0,
                }
            )
        if cases:
            rounds.append(
                {
                    "sense": sense,
                    "round": rd.name,
                    "cases": len(cases),
                    "reject_rate": (
                        sum(c["rejected_turns"] for c in cases)
                        / max(1, sum(c["turns"] for c in cases))
                    ),
                    "deadlock3_n": sum(1 for c in cases if c["deadlock3"]),
                }
            )

    dump_n = cerr_tot.get("dump", 0) + cerr_tot.get("sin_texto", 0)
    cerr_n = sum(cerr_tot.values()) or 1
    by_sense = defaultdict(list)
    for h in score_hits:
        by_sense[h["sense"]].append(h["frac"])

    return {
        "root": str(root),
        "rounds_n": len(rounds),
        "cases_n": cases_n,
        "turn_reject_rate": (reject_n / turn_n) if turn_n else 0.0,
        "turns_n": turn_n,
        "rejected_turns_n": reject_n,
        "cerrado_classes": dict(cerr_tot),
        "cerrado_dump_frac": dump_n / cerr_n,
        "cerrado_niega_frac": cerr_tot.get("niega", 0) / cerr_n,
        "cerrado_afirma_frac": cerr_tot.get("afirma", 0) / cerr_n,
        "deadlock3_cases": deadlock_cases,
        "jobs_vs_plan_n": len(jobs_vs_plan),
        "jobs_eq_plan_frac": (
            sum(1 for j in jobs_vs_plan if j["jobs_run"] >= j["plan_fases"])
            / len(jobs_vs_plan)
            if jobs_vs_plan
            else None
        ),
        "score_by_sense_mean": {
            s: (sum(v) / len(v) if v else 0.0) for s, v in sorted(by_sense.items())
        },
        "rounds": rounds,
        "jobs_vs_plan_sample": jobs_vs_plan[:20],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "root",
        nargs="?",
        default=str(
            Path(__file__).resolve().parents[2] / ".tuide/ai/l2_wave"
        ),
        help="Directorio con sense*/round_*",
    )
    ap.add_argument("-o", "--out", type=Path, help="Escribe JSON resumen")
    ap.add_argument("--json", action="store_true", help="Solo JSON a stdout")
    args = ap.parse_args()
    root = Path(args.root)
    if not root.is_dir():
        print(f"no dir: {root}", file=sys.stderr)
        return 2
    summary = analyze_tree(root)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
    if args.json or args.out:
        if args.json:
            print(json.dumps(summary, ensure_ascii=False, indent=2))
        else:
            print(
                json.dumps(
                    {
                        k: summary[k]
                        for k in (
                            "rounds_n",
                            "cases_n",
                            "turn_reject_rate",
                            "cerrado_dump_frac",
                            "cerrado_niega_frac",
                            "cerrado_afirma_frac",
                            "deadlock3_cases",
                            "jobs_eq_plan_frac",
                            "score_by_sense_mean",
                        )
                    },
                    ensure_ascii=False,
                    indent=2,
                )
            )
    else:
        print(f"rounds={summary['rounds_n']} cases={summary['cases_n']}")
        print(f"turn_reject_rate={summary['turn_reject_rate']:.3f}")
        print(f"cerrado dump/niega/afirma="
              f"{summary['cerrado_dump_frac']:.3f}/"
              f"{summary['cerrado_niega_frac']:.3f}/"
              f"{summary['cerrado_afirma_frac']:.3f}")
        print(f"deadlock3_cases={summary['deadlock3_cases']}")
        print(f"jobs_eq_plan_frac={summary['jobs_eq_plan_frac']}")
        print("score_by_sense_mean=", json.dumps(summary["score_by_sense_mean"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
