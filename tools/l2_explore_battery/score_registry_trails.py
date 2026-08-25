#!/usr/bin/env python3
"""Score registry-query --trails vs fixture expected_stems / trap_stems.

Cuts at T* (no LLM). A case hits if any expected stem appears in trail hops.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

# Fixture expected_stems miss the operational pole on some cases.
OPERATIONAL_EXTRA: dict[str, list[str]] = {
    "13_lsp_auto_restart": ["lsp_symbol_provider"],
    "20_cancel_ai_generation": ["busy_strip"],
}


def load_cases(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_json(path: Path) -> dict:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def path_stem(path: str) -> str:
    if not path:
        return ""
    return Path(path.replace("\\", "/")).stem.lower()


def hop_stems(hop: dict) -> list[str]:
    out: list[str] = []
    st = str(hop.get("stem") or "").strip().lower()
    if st:
        out.append(st)
    ps = path_stem(str(hop.get("path") or ""))
    if ps and ps not in out:
        out.append(ps)
    nid = str(hop.get("id") or "").strip()
    if nid.startswith("fn:") or nid.startswith("latch:") or nid.startswith("ctrl:"):
        body = nid.split(":", 1)[1]
        # fn:src/foo/bar.cpp:symbol  /  latch:stem:member  /  ctrl:path:line:kind
        if nid.startswith("latch:"):
            lat = body.split(":", 1)[0].lower()
            if lat and lat not in out:
                out.append(lat)
        else:
            colon = body.rfind(":")
            pathish = body[:colon] if colon > 0 else body
            # ctrl ids are path:line:kind — strip trailing :kind and :line
            if nid.startswith("ctrl:"):
                parts = body.split(":")
                if len(parts) >= 3:
                    pathish = ":".join(parts[:-2])
            ps2 = path_stem(pathish)
            if ps2 and ps2 not in out:
                out.append(ps2)
    return out


def stem_hit(needle: str, hay: list[str], *, trap: bool = False) -> bool:
    n = (needle or "").strip().lower()
    if not n:
        return False
    for h in hay:
        hl = h.strip().lower()
        if not hl:
            continue
        if n == hl:
            return True
        if trap and len(n) < 8:
            continue
        if n in hl or hl in n:
            return True
    return False


def collect_trail_stems(data: dict) -> dict[str, list[str]]:
    trails = data.get("trails") or []
    by_t: dict[str, list[str]] = {}
    all_stems: list[str] = []
    t1: list[str] = []
    symbols: list[str] = []
    for t in trails:
        tid = str(t.get("id") or "")
        bag: list[str] = []
        for hop in t.get("hops") or []:
            bag.extend(hop_stems(hop if isinstance(hop, dict) else {}))
            sym = str(hop.get("symbol") or "")
            if sym:
                symbols.append(sym)
        by_t[tid] = bag
        all_stems.extend(bag)
        if tid == "T1" or (not t1 and trails and t is trails[0]):
            t1 = bag
    seeds: list[str] = []
    for h in data.get("seeds") or []:
        if not isinstance(h, dict):
            continue
        seeds.extend(hop_stems(h))
        if h.get("symbol"):
            symbols.append(str(h["symbol"]))
    return {
        "all": all_stems,
        "t1": t1,
        "seeds": seeds,
        "symbols": symbols,
        "by_t": by_t,
    }


def collect_constellation_stems(data: dict) -> dict[str, list[str]]:
    all_stems: list[str] = []
    primary: list[str] = []
    c1: list[str] = []
    for index, constellation in enumerate(data.get("constellations") or []):
        if not isinstance(constellation, dict):
            continue
        bag: list[str] = []
        for stem in constellation.get("primary_stems") or []:
            value = str(stem).strip().lower()
            if value:
                primary.append(value)
                bag.append(value)
        for stem in constellation.get("peripheral_stems") or []:
            value = str(stem).strip().lower()
            if value:
                bag.append(value)
        for node in constellation.get("nodes") or []:
            if isinstance(node, dict):
                bag.extend(hop_stems(node))
        all_stems.extend(bag)
        if index == 0:
            c1 = bag
    return {"all": all_stems, "primary": primary, "c1": c1}


def collect_macro_constellation_stems(data: dict) -> dict[str, list[str]]:
    all_stems: list[str] = []
    primary: list[str] = []
    by_rank: list[list[str]] = []
    for macro in data.get("macro_constellations") or []:
        if not isinstance(macro, dict):
            continue
        bag: list[str] = []
        for stem in macro.get("primary_stems") or []:
            value = str(stem).strip().lower()
            if value:
                primary.append(value)
                bag.append(value)
        for node in macro.get("nodes") or []:
            if isinstance(node, dict):
                bag.extend(hop_stems(node))
        all_stems.extend(bag)
        by_rank.append(bag)
    first = by_rank[0] if by_rank else []
    top3 = [stem for bag in by_rank[:3] for stem in bag]
    top5 = [stem for bag in by_rank[:5] for stem in bag]
    return {"all": all_stems, "primary": primary, "m1": first, "m3": top3, "m5": top5}


def hits_in(needles: list[str], hay: list[str], *, trap: bool = False) -> list[str]:
    return [n for n in needles if stem_hit(n, hay, trap=trap)]


def trail_preview(data: dict, max_trails: int = 5) -> list[str]:
    lines: list[str] = []
    for t in (data.get("trails") or [])[:max_trails]:
        hops = t.get("hops") or []
        parts: list[str] = []
        for hop in hops:
            if not isinstance(hop, dict):
                continue
            kind = hop.get("kind") or ""
            if kind == "fn":
                parts.append(str(hop.get("symbol") or hop.get("id") or "?"))
            elif kind == "latch":
                parts.append("latch:" + str(hop.get("symbol") or hop.get("id") or "?"))
            elif kind == "ctrl":
                cond = str(hop.get("cond") or "")
                if len(cond) > 40:
                    cond = cond[:40] + "…"
                parts.append("[if " + cond + "]" if cond else "[ctrl]")
            else:
                parts.append(str(hop.get("symbol") or hop.get("id") or kind))
        score = t.get("score")
        why = t.get("why") or ""
        head = str(t.get("id") or "?")
        if score is not None:
            try:
                head += f" ({int(float(score))})"
            except (TypeError, ValueError):
                pass
        lines.append(head + " " + " → ".join(parts) + ((" — " + why) if why else ""))
    return lines


def score_case(case: dict, data: dict, run_row: dict | None) -> dict:
    expected = list(case.get("expected_stems") or [])
    traps = list(case.get("trap_stems") or [])
    operational = list(dict.fromkeys(expected + OPERATIONAL_EXTRA.get(case["id"], [])))
    bags = collect_trail_stems(data)
    constellations = collect_constellation_stems(data)
    macros = collect_macro_constellation_stems(data)
    n_trails = len(data.get("trails") or [])
    n_constellations = len(data.get("constellations") or [])
    n_macros = len(data.get("macro_constellations") or [])
    err = (run_row or {}).get("error")
    if not data and not err:
        err = "no query_trails.json"

    exp_all = hits_in(expected, bags["all"])
    exp_t1 = hits_in(expected, bags["t1"])
    op_all = hits_in(operational, bags["all"])
    op_t1 = hits_in(operational, bags["t1"])
    cos_exp = hits_in(expected, bags["seeds"])
    cos_op = hits_in(operational, bags["seeds"])
    trap_all = hits_in(traps, bags["all"], trap=True)
    trap_t1 = hits_in(traps, bags["t1"], trap=True)
    const_exp = hits_in(expected, constellations["all"])
    const_primary = hits_in(expected, constellations["primary"])
    const_c1 = hits_in(expected, constellations["c1"])
    const_op = hits_in(operational, constellations["all"])
    const_trap = hits_in(traps, constellations["all"], trap=True)
    const_primary_trap = hits_in(traps, constellations["primary"], trap=True)
    const_c1_trap = hits_in(traps, constellations["c1"], trap=True)
    macro_exp = hits_in(expected, macros["all"])
    macro_m1 = hits_in(expected, macros["m1"])
    macro_m3 = hits_in(expected, macros["m3"])
    macro_m5 = hits_in(expected, macros["m5"])
    macro_trap = hits_in(traps, macros["all"], trap=True)
    macro_m1_trap = hits_in(traps, macros["m1"], trap=True)

    hit = bool(exp_all) if expected else n_trails > 0
    op_hit = bool(op_all) if operational else hit
    t1_hit = bool(exp_t1) if expected else False
    empty = n_trails == 0

    return {
        "id": case["id"],
        "complexity": case.get("complexity"),
        "hit": hit,
        "op_hit": op_hit,
        "t1_hit": t1_hit,
        "cosine_hit": bool(cos_exp) if expected else False,
        "cosine_op_hit": bool(cos_op) if operational else False,
        "trap": bool(trap_all),
        "trap_t1": bool(trap_t1),
        "const_hit": bool(const_exp) if expected else n_constellations > 0,
        "const_primary_hit": bool(const_primary) if expected else False,
        "const_c1_hit": bool(const_c1) if expected else False,
        "const_op_hit": bool(const_op) if operational else n_constellations > 0,
        "const_trap": bool(const_trap),
        "const_primary_trap": bool(const_primary_trap),
        "const_c1_trap": bool(const_c1_trap),
        "const_recovery": bool(const_exp) and not hit,
        "macro_hit": bool(macro_exp) if expected else n_macros > 0,
        "macro_m1_hit": bool(macro_m1) if expected else False,
        "macro_m3_hit": bool(macro_m3) if expected else False,
        "macro_m5_hit": bool(macro_m5) if expected else False,
        "macro_trap": bool(macro_trap),
        "macro_m1_trap": bool(macro_m1_trap),
        "empty": empty,
        "n_trails": n_trails,
        "n_constellations": n_constellations,
        "n_macros": n_macros,
        "subgraph_nodes": data.get("subgraph_nodes"),
        "subgraph_facts": data.get("subgraph_facts"),
        "expected": expected,
        "operational": operational,
        "expected_hits": exp_all,
        "expected_t1_hits": exp_t1,
        "op_hits": op_all,
        "op_t1_hits": op_t1,
        "cosine_hits": cos_exp,
        "cosine_op_hits": cos_op,
        "trap_hits": trap_all,
        "trap_t1_hits": trap_t1,
        "const_hits": const_exp,
        "const_primary_hits": const_primary,
        "const_c1_hits": const_c1,
        "const_op_hits": const_op,
        "const_trap_hits": const_trap,
        "const_primary_trap_hits": const_primary_trap,
        "const_c1_trap_hits": const_c1_trap,
        "macro_hits": macro_exp,
        "macro_m1_hits": macro_m1,
        "macro_m3_hits": macro_m3,
        "macro_m5_hits": macro_m5,
        "macro_trap_hits": macro_trap,
        "macro_m1_trap_hits": macro_m1_trap,
        "preview": trail_preview(data),
        "exit": (run_row or {}).get("exit"),
        "error": err,
    }


def summarize(rows: list[dict]) -> dict:
    n = len(rows)
    def rate(key: str) -> float:
        if n == 0:
            return 0.0
        return sum(1 for r in rows if r.get(key)) / n

    return {
        "n": n,
        "hit": sum(1 for r in rows if r.get("hit")),
        "op_hit": sum(1 for r in rows if r.get("op_hit")),
        "t1_hit": sum(1 for r in rows if r.get("t1_hit")),
        "cosine_hit": sum(1 for r in rows if r.get("cosine_hit")),
        "trap": sum(1 for r in rows if r.get("trap")),
        "trap_t1": sum(1 for r in rows if r.get("trap_t1")),
        "const_hit": sum(1 for r in rows if r.get("const_hit")),
        "const_primary_hit": sum(1 for r in rows if r.get("const_primary_hit")),
        "const_c1_hit": sum(1 for r in rows if r.get("const_c1_hit")),
        "const_op_hit": sum(1 for r in rows if r.get("const_op_hit")),
        "const_trap": sum(1 for r in rows if r.get("const_trap")),
        "const_primary_trap": sum(1 for r in rows if r.get("const_primary_trap")),
        "const_c1_trap": sum(1 for r in rows if r.get("const_c1_trap")),
        "const_recovery": sum(1 for r in rows if r.get("const_recovery")),
        "macro_hit": sum(1 for r in rows if r.get("macro_hit")),
        "macro_m1_hit": sum(1 for r in rows if r.get("macro_m1_hit")),
        "macro_m3_hit": sum(1 for r in rows if r.get("macro_m3_hit")),
        "macro_m5_hit": sum(1 for r in rows if r.get("macro_m5_hit")),
        "macro_trap": sum(1 for r in rows if r.get("macro_trap")),
        "macro_m1_trap": sum(1 for r in rows if r.get("macro_m1_trap")),
        "empty": sum(1 for r in rows if r.get("empty")),
        "hit_rate": rate("hit"),
        "op_hit_rate": rate("op_hit"),
        "t1_hit_rate": rate("t1_hit"),
        "cosine_hit_rate": rate("cosine_hit"),
        "trap_rate": rate("trap"),
        "const_hit_rate": rate("const_hit"),
        "const_primary_hit_rate": rate("const_primary_hit"),
        "macro_hit_rate": rate("macro_hit"),
        "macro_m1_hit_rate": rate("macro_m1_hit"),
        "errors": sum(1 for r in rows if r.get("error")),
    }


def format_console(rows: list[dict], summary: dict) -> str:
    lines = [
        f"registry-trails n={summary['n']}  "
        f"hit={summary['hit']}/{summary['n']} ({summary['hit_rate']:.0%})  "
        f"op_hit={summary['op_hit']}/{summary['n']} ({summary['op_hit_rate']:.0%})  "
        f"t1={summary['t1_hit']}/{summary['n']}  "
        f"cosine={summary['cosine_hit']}/{summary['n']}  "
        f"trap={summary['trap']} trap_t1={summary['trap_t1']}  "
        f"empty={summary['empty']} err={summary['errors']}",
        f"constellations hit={summary['const_hit']}/{summary['n']} "
        f"({summary['const_hit_rate']:.0%}) primary={summary['const_primary_hit']}/{summary['n']} "
        f"C1={summary['const_c1_hit']}/{summary['n']} op={summary['const_op_hit']}/{summary['n']} "
        f"recoveries={summary['const_recovery']} trap={summary['const_trap']} "
        f"primary_trap={summary['const_primary_trap']} C1_trap={summary['const_c1_trap']}",
        f"macroconstellations hit={summary['macro_hit']}/{summary['n']} "
        f"({summary['macro_hit_rate']:.0%}) M1={summary['macro_m1_hit']}/{summary['n']} "
        f"M3={summary['macro_m3_hit']}/{summary['n']} M5={summary['macro_m5_hit']}/{summary['n']} "
        f"trap={summary['macro_trap']} M1_trap={summary['macro_m1_trap']}",
        "",
        f"{'id':<32} {'hit':<4} {'op':<4} {'T1':<4} {'C':<4} {'CP':<4} {'C1':<4} "
        f"{'trap':<5} trails/const  expected_hits",
    ]
    for r in rows:
        mark = lambda k: "Y" if r.get(k) else ("." if not r.get("error") else "E")
        hits = ",".join(r.get("expected_hits") or []) or "—"
        trap = ",".join(r.get("trap_hits") or [])
        extra = f"  trap={trap}" if trap else ""
        lines.append(
            f"{r['id']:<32} {mark('hit'):<4} {mark('op_hit'):<4} {mark('t1_hit'):<4} "
            f"{mark('const_hit'):<4} {mark('const_primary_hit'):<4} {mark('const_c1_hit'):<4} "
            f"{mark('trap'):<5} {r.get('n_trails') or 0}/{r.get('n_constellations') or 0:<6} "
            f"{hits}{extra}"
        )
        for prev in r.get("preview") or []:
            lines.append(f"    {prev}")
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", type=Path, required=True)
    ap.add_argument("--round-dir", type=Path, required=True)
    args = ap.parse_args()

    cases = load_cases(args.cases)
    rows_by_id: dict[str, dict] = {}
    results = args.round_dir / "results.jsonl"
    if results.exists():
        for line in results.read_text(errors="replace").splitlines():
            if not line.strip():
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError:
                continue
            cid = row.get("id")
            if cid:
                rows_by_id[cid] = row

    scored: list[dict] = []
    for case in cases:
        cid = case["id"]
        case_dir = args.round_dir / cid
        if not case_dir.exists() and cid not in rows_by_id:
            continue
        data = load_json(case_dir / "query_trails.json")
        scored.append(score_case(case, data, rows_by_id.get(cid)))

    summary = summarize(scored)
    report = {"summary": summary, "cases": scored}
    (args.round_dir / "trails_score.json").write_text(
        json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    text = format_console(scored, summary)
    (args.round_dir / "trails_score.txt").write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
