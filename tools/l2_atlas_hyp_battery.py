#!/usr/bin/env python3
"""Batería 20 casos: L1 → atlas cards → atlas-survey (inspect + hyp). Para en hipótesis."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.l2_core5_battery import (  # noqa: E402
    CLI,
    env_base,
    registry_embed_once,
    registry_ingest_map,
    run_l1_battery,
)
from tools.l2_explore_battery.pf_battery_lib import load_cases, pf_search_terms  # noqa: E402

PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
KNOBS = ROOT / "tools/l2_explore_battery/mechanism_pack/knobs_atlas.json"
BATTERY_ROOT = ROOT / ".tuide/ai/l2_explore_battery"


def query_from_pf(pf: dict, prompt: str) -> str:
    terms = list(pf_search_terms(pf))
    for sec in pf.get("secondary_anchors") or []:
        if not isinstance(sec, dict):
            continue
        for t in sec.get("search_terms") or []:
            s = str(t).strip()
            if s and s not in terms:
                terms.append(s)
    if not terms:
        return prompt[:120]
    return " ".join(terms[:8])


def hyp_stems(hypotheses: list) -> list[str]:
    out: list[str] = []
    for h in hypotheses or []:
        if not isinstance(h, dict):
            continue
        for t in h.get("search_terms") or []:
            s = str(t).strip()
            if s and s not in out:
                out.append(s)
        slots = h.get("slots") or {}
        if isinstance(slots, dict):
            for slot in slots.values():
                if isinstance(slot, dict):
                    s = str(slot.get("stem") or "").strip()
                    if s and s not in out:
                        out.append(s)
    return out


def any_overlap(needles: list[str], hay: list[str]) -> list[str]:
    hits = []
    hlow = [x.lower() for x in hay]
    for n in needles:
        nl = n.lower()
        if any(nl == h or nl in h or h in nl for h in hlow):
            hits.append(n)
    return hits


def zone_stems(payload: dict) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for z in payload.get("zones") or []:
        if not isinstance(z, dict):
            continue
        zid = str(z.get("id") or "")
        stems = [str(s) for s in (z.get("primary_stems") or []) if s]
        out[zid] = stems
    return out


def run_cards(case: dict, l1_dir: Path, cards_dir: Path, env: dict[str, str]) -> dict:
    cid = case["id"]
    cards_dir.mkdir(parents=True, exist_ok=True)
    map_path = l1_dir / "map_last.md"
    pf_path = l1_dir / "problem_frame.json"
    payload_path = cards_dir / "judge_cards.json"
    atlas_path = cards_dir / "atlas.md"
    if not map_path.exists() or not pf_path.exists():
        return {"id": cid, "ok": False, "error": "missing_l1"}
    pf = json.loads(pf_path.read_text(encoding="utf-8"))
    query = query_from_pf(pf, case["prompt"])
    (cards_dir / "graph_query.txt").write_text(query + "\n", encoding="utf-8")
    cmd = [
        str(CLI),
        "registry-query",
        "--trails",
        "--top",
        "16",
        "--hops",
        "2",
        "--threads",
        "5",
        "--map",
        str(map_path),
        "--map-top",
        "15",
        "--judge-cards-md",
        "--view",
        "atlas",
        "--judge-knobs",
        str(KNOBS),
        "--judge-cards-json-out",
        str(payload_path),
        query,
    ]
    p = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True, timeout=400)
    (cards_dir / "cards.log").write_text(
        (p.stderr or "") + "\n" + (p.stdout or ""), encoding="utf-8"
    )
    if p.returncode != 0 or not (p.stdout or "").startswith("# causal_atlas_v1"):
        return {"id": cid, "ok": False, "error": f"cards_rc={p.returncode}", "query": query}
    atlas_path.write_text(p.stdout, encoding="utf-8")
    payload = json.loads(payload_path.read_text(encoding="utf-8"))
    return {
        "id": cid,
        "ok": True,
        "query": query,
        "n_zones": len(payload.get("zones") or []),
        "atlas_chars": len(p.stdout),
    }


def run_survey(case_id: str, cards_root: Path, survey_root: Path, env: dict[str, str]) -> dict:
    cmd = [
        str(CLI),
        "atlas-survey",
        "--cards-root",
        str(cards_root),
        "--out",
        str(survey_root),
        "--only",
        case_id,
    ]
    p = subprocess.run(cmd, cwd=str(ROOT), env=env, capture_output=True, text=True, timeout=240)
    case_out = survey_root / case_id
    (case_out / "survey_run.log").write_text(
        (p.stderr or "") + "\n" + (p.stdout or ""), encoding="utf-8"
    )
    summary_path = case_out / "summary.json"
    if not summary_path.exists():
        return {"id": case_id, "ok": False, "error": f"survey_rc={p.returncode}",
                "stderr": (p.stderr or "")[-400:]}
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    summary["survey_rc"] = p.returncode
    return summary


def score_case(case: dict, l1_dir: Path, cards_dir: Path, survey_dir: Path, row: dict) -> dict:
    expected = list(case.get("expected_stems") or [])
    traps = list(case.get("trap_stems") or [])
    pf = {}
    pf_path = l1_dir / "problem_frame.json"
    if pf_path.exists():
        pf = json.loads(pf_path.read_text(encoding="utf-8"))
    payload = {}
    payload_path = cards_dir / "judge_cards.json"
    if payload_path.exists():
        payload = json.loads(payload_path.read_text(encoding="utf-8"))
    zstems = zone_stems(payload)
    atlas_stems = [s for stems in zstems.values() for s in stems]
    shortlist = list(row.get("shortlist") or [])
    inspect_stems = []
    for zid in row.get("inspect_zones") or shortlist:
        inspect_stems.extend(zstems.get(str(zid), []))
    hyps = list(row.get("hypotheses") or [])
    hstems = hyp_stems(hyps)
    raw_n = 0
    raw_path = survey_dir / "hyp_raw.txt"
    if raw_path.exists():
        try:
            raw = json.loads(raw_path.read_text(encoding="utf-8"))
            if isinstance(raw, dict):
                raw_n = len(raw.get("hypotheses") or [])
        except json.JSONDecodeError:
            txt = raw_path.read_text(encoding="utf-8", errors="replace")
            raw_n = txt.count('"claim"')
    claims = [str(h.get("claim") or "") for h in hyps if isinstance(h, dict)]
    gold_atlas = any_overlap(expected, atlas_stems)
    gold_inspect = any_overlap(expected, inspect_stems)
    gold_hyp = any_overlap(expected, hstems)
    trap_hyp = any_overlap(traps, hstems)
    return {
        "id": case["id"],
        "complexity": case.get("complexity"),
        "ok": bool(row.get("ok") and row.get("hyp_ok")),
        "l1_terms": pf_search_terms(pf),
        "query": row.get("query") or query_from_pf(pf, case["prompt"]) if pf else "",
        "n_zones": len(payload.get("zones") or []),
        "shortlist": shortlist,
        "inspect_zones": row.get("inspect_zones") or [],
        "hyp_ok": bool(row.get("hyp_ok")),
        "hyp_error": row.get("hyp_error") or row.get("error") or "",
        "n_hyps_kept": len(hyps),
        "n_hyps_raw": raw_n,
        "claims": claims,
        "hyp_stems": hstems,
        "gold_in_atlas": gold_atlas,
        "gold_in_inspect": gold_inspect,
        "gold_in_hyp": gold_hyp,
        "trap_in_hyp": trap_hyp,
        "expected": expected,
        "traps": traps,
        "survey_why": row.get("why") or "",
        "cover_covers": row.get("cover_covers"),
        "cover_add": row.get("cover_add") or [],
        "inspect_r1_zones": row.get("inspect_r1_zones") or [],
        "hyp_why": row.get("hyp_why") or "",
        "elapsed_ms": row.get("elapsed_ms"),
        "hyp_ms": row.get("hyp_ms"),
    }


def verdict_bucket(row: dict) -> str:
    if not row.get("hyp_ok"):
        return "fail_parse"
    if row.get("gold_in_hyp"):
        return "gold_hyp"
    if row.get("trap_in_hyp") and not row.get("gold_in_hyp"):
        return "trap_hyp"
    if row.get("gold_in_inspect"):
        return "gold_inspect_miss_hyp"
    if row.get("gold_in_atlas"):
        return "gold_atlas_not_inspected"
    return "no_gold"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="atlas20_hyp")
    ap.add_argument("--from-round", type=Path, default=None, help="L1 round with map_last.md")
    ap.add_argument("--skip-l1", action="store_true")
    ap.add_argument("--skip-ingest", action="store_true")
    ap.add_argument("--skip-cards", action="store_true")
    ap.add_argument("--cards-from", type=Path, default=None, help="Reuse judge_cards.json from another round")
    ap.add_argument("--only", default="", help="Comma-separated case ids")
    ap.add_argument("--timeout-l1", type=int, default=300)
    args = ap.parse_args()

    cases = load_cases(PROMPTS)
    if args.only:
        wanted = {x.strip() for x in args.only.split(",") if x.strip()}
        cases = [c for c in cases if c["id"] in wanted]
        if not cases:
            raise SystemExit(f"no cases matched --only {args.only}")
    env = env_base()
    hyp_root = BATTERY_ROOT / f"round_{args.label}"
    cards_root = hyp_root / "cards"
    if args.cards_from is not None:
        cards_from = args.cards_from
        if not cards_from.is_absolute():
            cards_from = ROOT / cards_from
        cards_root = cards_from
        args.skip_cards = True
    survey_root = hyp_root / "survey"
    hyp_root.mkdir(parents=True, exist_ok=True)

    if args.skip_l1 or args.from_round:
        l1_root = args.from_round
        if l1_root is None:
            raise SystemExit("--from-round required with --skip-l1")
        if not l1_root.is_absolute():
            l1_root = ROOT / l1_root
    else:
        print("==== L1 20 casos ====", flush=True)
        l1_root = run_l1_battery(args.label + "_l1", PROMPTS, args.only, args.timeout_l1)

    (hyp_root / "l1_round.txt").write_text(str(l1_root) + "\n", encoding="utf-8")

    if args.skip_ingest:
        print("==== skip ingest (registry ya poblado) ====", flush=True)
    else:
        print("==== ingest maps ====", flush=True)
        for case in cases:
            mp = l1_root / case["id"] / "map_last.md"
            if mp.exists():
                rc = registry_ingest_map(mp, env)
                print(f"  ingest {case['id']} rc={rc}", flush=True)

    print("==== registry-embed ====", flush=True)
    embed_rc = registry_embed_once(hyp_root, env)
    print(f"  embed rc={embed_rc}", flush=True)

    card_rows = []
    for case in cases:
        print(f"==== cards {case['id']} ====", flush=True)
        cards_dir = cards_root / case["id"]
        if args.skip_cards and (cards_dir / "judge_cards.json").exists():
            payload = json.loads((cards_dir / "judge_cards.json").read_text(encoding="utf-8"))
            qpath = cards_dir / "graph_query.txt"
            query = qpath.read_text(encoding="utf-8").strip() if qpath.exists() else ""
            card_rows.append(
                {
                    "id": case["id"],
                    "ok": True,
                    "n_zones": len(payload.get("zones") or []),
                    "query": query,
                }
            )
            continue
        row = run_cards(case, l1_root / case["id"], cards_dir, env)
        card_rows.append(row)
        print(f"  {row}", flush=True)

    survey_rows = []
    for case in cases:
        cid = case["id"]
        cards_ok = next((r for r in card_rows if r["id"] == cid), {})
        if not cards_ok.get("ok"):
            survey_rows.append({"id": cid, "ok": False, "error": cards_ok.get("error", "no_cards")})
            print(f"==== skip survey {cid} (no cards) ====", flush=True)
            continue
        print(f"==== survey+hyp {cid} ====", flush=True)
        row = run_survey(cid, cards_root, survey_root, env)
        if cards_ok.get("query"):
            row["query"] = cards_ok["query"]
        survey_rows.append(row)
        print(
            f"  ok={row.get('ok')} hyp_ok={row.get('hyp_ok')} "
            f"r1={row.get('inspect_r1_zones')} add={row.get('cover_add')} covers={row.get('cover_covers')} "
            f"shortlist={row.get('shortlist')} n_hyps={len(row.get('hypotheses') or [])}",
            flush=True,
        )

    scored = []
    for case in cases:
        cid = case["id"]
        srow = next((r for r in survey_rows if r.get("id") == cid), {"id": cid, "ok": False})
        scored.append(score_case(case, l1_root / cid, cards_root / cid, survey_root / cid, srow))

    for s in scored:
        s["bucket"] = verdict_bucket(s)

    summary = {
        "label": args.label,
        "l1_round": str(l1_root),
        "n": len(scored),
        "hyp_ok": sum(1 for s in scored if s.get("hyp_ok")),
        "gold_hyp": sum(1 for s in scored if s["bucket"] == "gold_hyp"),
        "trap_hyp": sum(1 for s in scored if s["bucket"] == "trap_hyp"),
        "gold_inspect_miss_hyp": sum(1 for s in scored if s["bucket"] == "gold_inspect_miss_hyp"),
        "gold_atlas_not_inspected": sum(1 for s in scored if s["bucket"] == "gold_atlas_not_inspected"),
        "fail_parse": sum(1 for s in scored if s["bucket"] == "fail_parse"),
        "no_gold": sum(1 for s in scored if s["bucket"] == "no_gold"),
        "rows": scored,
    }
    (hyp_root / "hyp_score.json").write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("==== SCORE ====", flush=True)
    print(
        json.dumps({k: summary[k] for k in summary if k != "rows"}, indent=2),
        flush=True,
    )
    for s in scored:
        print(
            f"{s['id']:32} {s['bucket']:24} gold_hyp={s['gold_in_hyp']} "
            f"inspect={s['gold_in_inspect']} atlas={s['gold_in_atlas']} "
            f"r1={s.get('inspect_r1_zones')} add={s.get('cover_add')} "
            f"stems={s['hyp_stems'][:4]}",
            flush=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
