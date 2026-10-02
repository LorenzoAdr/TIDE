#!/usr/bin/env python3
"""PoC: zone cards (8) → causal anchor hyp → entityness filter (no L1 pass-B).

Pipeline for one case (default 04_ai_thinking_status_bar):
  1. Ensure map_last.md (reuse --map or L1 debug)
  2. registry-query --trails → judge_cards.json/md (max 8 zones)
  3. zone-judge-battery --two-pass --only CASE  (anchor LLM on zones)
  4. Convert anchors/expand_from → problem_frame.anchor_hypotheses
  5. entityness-probe → f1_anchor | classic_scan

Usage:
  python3 tools/l2_explore_battery/poc_zonal_hyp_hunt.py \\
    --only 04_ai_thinking_status_bar \\
    --map .tuide/ai/l2_explore_battery/poc_04_thinking/map_last.md
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/l2_harness_cli"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
CORE5 = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human_core5.json"


def load_case(case_id: str) -> dict:
    for path in (CORE5, PROMPTS):
        cases = json.loads(path.read_text(encoding="utf-8"))
        for c in cases:
            if c.get("id") == case_id:
                return c
    raise SystemExit(f"case not found: {case_id}")


def stem_from_target(target: str) -> str:
    """path:Symbol or path → basename stem / symbol name."""
    t = (target or "").strip()
    if not t:
        return ""
    if ":" in t:
        path, sym = t.rsplit(":", 1)
        if sym and re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", sym):
            return sym
        t = path
    base = Path(t).name
    for ext in (".cpp", ".hpp", ".cc", ".h", ".c"):
        if base.endswith(ext):
            return base[: -len(ext)]
    return base


def run(cmd: list[str], log_path: Path, timeout: int = 600) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    env["TUIDE_ROOT"] = str(ROOT)
    print(">", " ".join(cmd[:8]), "…", flush=True)
    p = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, timeout=timeout)
    log_path.write_text((p.stderr or "") + "\n" + (p.stdout or ""), encoding="utf-8")
    return p


def pf_from_anchor_decision(case: dict, triage: dict, payload: dict) -> dict:
    """Map causal_zone_anchor_v1 anchors → problem_frame_v1 with failure hyps."""
    zones_by_id = {}
    for z in payload.get("zones") or []:
        if isinstance(z, dict) and z.get("id"):
            zones_by_id[str(z["id"])] = z

    hyps = []
    for a in triage.get("anchors") or []:
        if not isinstance(a, dict):
            continue
        zid = str(a.get("id") or "")
        role = str(a.get("role_guess") or "affected")
        expand = a.get("expand_from") or []
        terms: list[str] = []
        for t in expand:
            s = stem_from_target(str(t))
            if s and s not in terms:
                terms.append(s)
        zone = zones_by_id.get(zid) or {}
        for st in zone.get("primary_stems") or []:
            s = str(st)
            if s and s not in terms:
                terms.append(s)
        if not terms:
            for st in zone.get("core_stems") or []:
                s = str(st)
                if s and s not in terms:
                    terms.append(s)
        claim = str(a.get("thread") or triage.get("hypothesis") or a.get("explains") or "")
        if len(claim) < 12:
            claim = str(triage.get("hypothesis") or "anchor from zone " + zid)
        slot = {
            "affected": None,
            "control": None,
            "trigger": None,
            "cleanup": None,
        }
        locus = {
            "stem": terms[0] if terms else "",
            "path_symbol": str(expand[0]) if expand else (terms[0] if terms else ""),
            "from_map": 0,
        }
        # Map role_guess → slot bucket
        r = role.lower()
        if r in ("state_owner", "state", "control"):
            slot["control"] = locus
            anchor_role = "control"
        elif r == "trigger":
            slot["trigger"] = locus
            anchor_role = "trigger"
        elif r == "cleanup":
            slot["cleanup"] = locus
            anchor_role = "cleanup"
        else:
            slot["affected"] = locus
            anchor_role = "affected"
        hyps.append(
            {
                "claim": claim[:200],
                "objective": claim[:160],
                "search_terms": terms[:4],
                "mechanism_slot": role,
                "gap": role,
                "anchor_role": anchor_role,
                "falsify_by": str(a.get("does_not_explain") or "")[:160],
                "why": str(a.get("explains") or triage.get("why") or "")[:160],
                "slots": slot,
            }
        )

    # Fallback: whole hypothesis text + shortlist stems from payload top zone
    if not hyps and triage.get("hypothesis"):
        terms = []
        for z in (payload.get("zones") or [])[:1]:
            for st in (z.get("primary_stems") or [])[:3]:
                terms.append(str(st))
        hyps.append(
            {
                "claim": str(triage.get("hypothesis")),
                "search_terms": terms,
                "anchor_role": "affected",
                "gap": "affected",
                "slots": {
                    "affected": {
                        "stem": terms[0] if terms else "",
                        "path_symbol": terms[0] if terms else "",
                        "from_map": 0,
                    },
                    "control": None,
                    "trigger": None,
                    "cleanup": None,
                },
                "why": str(triage.get("why") or ""),
            }
        )

    prompt = case.get("prompt") or ""
    return {
        "schema": "problem_frame_v1",
        "problem_kind": case.get("problem_kind_expected") or "implement",
        "problem_frame": prompt[:240],
        "instruction": prompt,
        "primary_anchor": {
            "kind": "feature",
            "objective": "from zonal hyp PoC",
            "search_terms": [],
        },
        "anchor_confidence": "low",
        "provenance": "zonal_hyp_poc",
        "anchor_hypotheses": hyps,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="04_ai_thinking_status_bar")
    ap.add_argument("--map", type=Path, default=None, help="Existing map_last.md")
    ap.add_argument(
        "--out",
        type=Path,
        default=ROOT / ".tuide/ai/l2_explore_battery/poc_zonal_04",
    )
    ap.add_argument("--skip-cards", action="store_true")
    ap.add_argument("--skip-judge", action="store_true")
    args = ap.parse_args()

    if not CLI.exists():
        print("missing", CLI, file=sys.stderr)
        return 2

    case = load_case(args.only)
    out = args.out if args.out.is_absolute() else ROOT / args.out
    out.mkdir(parents=True, exist_ok=True)
    case_dir = out / args.only
    case_dir.mkdir(parents=True, exist_ok=True)
    cards_root = out / "cards"
    cards_case = cards_root / args.only
    cards_case.mkdir(parents=True, exist_ok=True)
    judge_root = out / "judge"

    # 1) map
    map_path = args.map
    if map_path is None:
        map_path = case_dir / "map_last.md"
    elif not map_path.is_absolute():
        map_path = ROOT / map_path
    if not map_path.exists():
        print("FAIL: need --map (run L1 first)", file=sys.stderr)
        return 2
    # zone-judge expects cards_root/CASE/
    dest_map = case_dir / "map_last.md"
    if map_path.resolve() != dest_map.resolve():
        dest_map.write_bytes(map_path.read_bytes())

    # 2) zone cards
    payload_path = cards_case / "judge_cards.json"
    cards_md = cards_case / "judge_cards.md"
    if not args.skip_cards or not payload_path.exists():
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
            "--judge-cards-json-out",
            str(payload_path),
            case["prompt"],
        ]
        p = run(cmd, cards_case / "cards.log", timeout=400)
        if p.returncode != 0:
            print("FAIL cards rc=", p.returncode, file=sys.stderr)
            print(p.stderr[-2000:] if p.stderr else "", file=sys.stderr)
            return 1
        cards_md.write_text(p.stdout, encoding="utf-8")
        print("cards ok →", payload_path)

    payload = json.loads(payload_path.read_text(encoding="utf-8"))
    zones = payload.get("zones") or []
    print(f"zones={len(zones)}")
    for z in zones[:8]:
        print(
            f"  {z.get('id')}: stems={(z.get('primary_stems') or z.get('core_stems') or [])[:4]}"
        )

    # 3) causal anchor (two-pass battery, only this case)
    if not args.skip_judge:
        cmd = [
            str(CLI),
            "zone-judge-battery",
            "--cards-root",
            str(cards_root),
            "--out",
            str(judge_root),
            "--only",
            args.only,
            "--two-pass",
        ]
        p = run(cmd, case_dir / "judge.log", timeout=900)
        if p.returncode != 0:
            print("FAIL judge rc=", p.returncode, file=sys.stderr)
            # continue if triage files exist

    triage_path = judge_root / args.only / "triage_decision.json"
    # two-pass epistemic writes triage as anchor_decision or triage_raw
    alt = list((judge_root / args.only).glob("*decision*.json")) if (judge_root / args.only).exists() else []
    triage = {}
    for cand in [triage_path, *(alt or [])]:
        if cand.exists():
            try:
                triage = json.loads(cand.read_text(encoding="utf-8"))
                triage_path = cand
                break
            except json.JSONDecodeError:
                continue
    # Also try parsing from triage_raw / anchor_raw
    if not triage.get("anchors") and not triage.get("hypothesis"):
        for name in ("anchor_raw.txt", "triage_raw.txt", "pass0_raw.txt", "anchor_decision.json"):
            rp = judge_root / args.only / name
            if not rp.exists():
                continue
            raw = rp.read_text(encoding="utf-8", errors="replace")
            if name.endswith(".json"):
                try:
                    triage = json.loads(raw)
                    break
                except json.JSONDecodeError:
                    pass
            else:
                m = re.search(r"\{[\s\S]*\}", raw)
                if m:
                    try:
                        triage = json.loads(m.group(0))
                        break
                    except json.JSONDecodeError:
                        pass

    print("triage source keys:", list(triage.keys())[:12])
    (case_dir / "triage_used.json").write_text(
        json.dumps(triage, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )

    # 4) PF + entityness
    pf = pf_from_anchor_decision(case, triage, payload)
    pf_path = case_dir / "problem_frame.json"
    pf_path.write_text(json.dumps(pf, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print("hyps:", len(pf.get("anchor_hypotheses") or []))
    for i, h in enumerate(pf.get("anchor_hypotheses") or []):
        print(f"  hyp_{i}: role={h.get('anchor_role')} terms={h.get('search_terms')} claim={str(h.get('claim'))[:80]}")

    ent_out = case_dir / "entityness.json"
    cmd = [
        str(CLI),
        "entityness-probe",
        "--workspace",
        str(ROOT),
        "--problem-frame-json",
        str(pf_path),
        "--query",
        case["prompt"][:160],
        "--out",
        str(ent_out),
    ]
    p = run(cmd, case_dir / "entityness.log", timeout=120)
    print(p.stdout[:1500] if p.stdout else p.stderr[:800])

    ent = {}
    if ent_out.exists():
        ent = json.loads(ent_out.read_text(encoding="utf-8"))
    gold = set(case.get("anchor_gold") or case.get("expected_stems") or [])
    print("=== SUMMARY ===")
    print("explore_mode:", ent.get("explore_mode"), "best:", ent.get("best_role"), ent.get("best_entityness"))
    for L in ent.get("links") or []:
        role = L.get("role")
        if role == "primary" or str(role).startswith("hyp_"):
            stem = str(L.get("owner_stem") or "")
            terms = L.get("search_terms") or []
            blob = " ".join([stem] + [str(t) for t in terms]).lower()
            hit = [g for g in gold if g.lower() in blob or any(g.lower()[:4] in str(t).lower() for t in terms)]
            print(
                f"  {role}: ent={L.get('entityness')} stem={stem} terms={terms} goldish={hit or '-'}"
            )
    # zone gold coverage
    zone_blob = json.dumps(zones).lower()
    print("gold in zone cards:", [g for g in gold if g.lower() in zone_blob])
    print("out:", out)
    return 0 if ent.get("explore_mode") else 1


if __name__ == "__main__":
    raise SystemExit(main())
