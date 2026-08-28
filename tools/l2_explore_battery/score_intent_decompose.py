#!/usr/bin/env python3
"""Score intent_decompose_v0 + per-unit entityness (prompt-fair rubric)."""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.pf_battery_lib import (  # noqa: E402
    DEFAULT_CASES,
    _fold_ascii_alnum,
    _term_grounded,
    load_cases,
    load_json,
)

DEFAULT_RUBRIC = Path(__file__).with_name("decompose_objective_rubric.json")
F1_THR = 0.45


def units_at_stop(decomp: dict[str, Any]) -> list[dict[str, Any]]:
    levels = decomp.get("levels") or []
    if not isinstance(levels, list) or not levels:
        return []
    stop = decomp.get("stop_level")
    if stop is None:
        stop = levels[-1].get("level", 0)
    for lv in levels:
        if isinstance(lv, dict) and int(lv.get("level", -1)) == int(stop):
            u = lv.get("units") or []
            return [x for x in u if isinstance(x, dict)]
    # fallback: last level
    u = levels[-1].get("units") if isinstance(levels[-1], dict) else []
    return [x for x in (u or []) if isinstance(x, dict)]


def shannon_entropy(weights: list[float]) -> float:
    s = sum(max(0.0, w) for w in weights)
    if s <= 1e-12:
        return 0.0
    h = 0.0
    for w in weights:
        p = max(0.0, w) / s
        if p > 0:
            h -= p * math.log(p, 2)
    return h


def cue_hit(cue: str, terms: list[str], query: str) -> bool:
    cf = _fold_ascii_alnum(cue)
    if len(cf) < 3:
        return False
    qf = _fold_ascii_alnum(query)
    # cue must be in the prompt to be required
    if cf not in qf and cf[:4] not in qf:
        return True  # N/A → treat as satisfied
    for t in terms:
        tf = _fold_ascii_alnum(str(t))
        if cf in tf or tf in cf or (len(cf) >= 4 and cf[:4] in tf):
            return True
    return False


def score_case(case: dict, case_dir: Path, rubric_case: dict) -> dict[str, Any]:
    cid = case["id"]
    prompt = case.get("prompt") or ""
    decomp = load_json(case_dir / "decompose.json")
    ent = load_json(case_dir / "entityness.json")
    reasons: list[str] = []
    notes: list[str] = []

    structural_ok = bool(decomp.get("schema") == "intent_decompose_v0" or decomp.get("levels"))
    if not decomp:
        return {
            "id": cid,
            "pass": False,
            "objective_class": rubric_case.get("objective_class"),
            "reasons": ["missing_decompose_json"],
            "na_reasons": [],
        }

    units = units_at_stop(decomp)
    all_terms: list[str] = []
    for u in units:
        all_terms.extend([str(t) for t in (u.get("search_terms") or [])])

    qf = _fold_ascii_alnum(prompt)
    grounded = [t for t in all_terms if _term_grounded(t, qf)]
    grounded_ratio = (len(grounded) / len(all_terms)) if all_terms else 0.0
    if all_terms and grounded_ratio < 0.5:
        reasons.append(f"low_grounded_ratio={grounded_ratio:.2f}")

    roles = [str(u.get("role") or "") for u in units]
    has_focal = any(r == "focal" for r in roles)
    has_ambient = any(r == "ambient" for r in roles)

    # Mixed ambient+focal inside one unit (terms bag) — always a partition fail if both roles claimed wrongly
    mixed_unit = False
    for u in units:
        role = str(u.get("role") or "")
        terms = [str(t).lower() for t in (u.get("search_terms") or [])]
        # Heuristic: unit labeled focal but only ambient-ish single tokens without symptom — soft note
        if role == "focal" and has_ambient:
            pass
        # Same unit should not be labeled with impossible dual — check label collision N/A
        if " " in " ".join(terms):
            mixed_unit = True
    # Explicit: any unit must not mix if we detect both ambient cue and focal cue in SAME unit
    # when rubric requires separation — use role tags: two roles cannot share one unit by schema;
    # fail if a single unit's terms include both a known ambient cue and required focal cue from prompt.
    ambient_cues = ("panel", "chat", "asistente", "ventana", "app")
    if rubric_case.get("require_ambient_focal_separation"):
        for u in units:
            terms_f = [_fold_ascii_alnum(str(t)) for t in (u.get("search_terms") or [])]
            has_a = any(any(a in t for a in ambient_cues) for t in terms_f)
            has_f_cue = False
            for cue in rubric_case.get("lexical_cues_any") or []:
                cf = _fold_ascii_alnum(cue)
                if any(cf in t or t in cf for t in terms_f):
                    has_f_cue = True
            if has_a and has_f_cue:
                reasons.append(f"mixed_ambient_focal_unit={u.get('id')}")
                mixed_unit = True

    if rubric_case.get("require_focal_unit") and not has_focal:
        reasons.append("missing_focal_unit")

    # Lexical cues: only required if present in prompt; fail if prompt has cue but no unit projects it
    cues = list(rubric_case.get("lexical_cues_any") or [])
    if cues:
        prompt_has = []
        for cue in cues:
            cf = _fold_ascii_alnum(cue)
            if cf in qf or (len(cf) >= 4 and cf[:4] in qf):
                prompt_has.append(cue)
        if prompt_has:
            if not any(cue_hit(c, all_terms, prompt) for c in prompt_has):
                reasons.append(f"lexical_cue_not_projected:{prompt_has}")
        else:
            notes.append("lexical_cues_not_in_prompt_na")

    # Entityness links (one per unit)
    links = ent.get("links") if isinstance(ent.get("links"), list) else []
    link_by_role = {str(L.get("role")): L for L in links if isinstance(L, dict)}
    ents = [float(L.get("entityness") or 0) for L in links if isinstance(L, dict)]
    entropy = shannon_entropy(ents) if ents else 0.0
    best_ent = float(ent.get("best_entityness") or (max(ents) if ents else 0.0))
    mode = str(ent.get("explore_mode") or "")
    best_role = str(ent.get("best_role") or "")

    # Focal entityness: primary if mapped from focal unit
    focal_ent = None
    if "primary" in link_by_role:
        # primary is focal when we mapped focal→primary
        focal_ent = float(link_by_role["primary"].get("entityness") or 0)
    for L in links:
        if not isinstance(L, dict):
            continue
        # match by objective label containing unit — fall back to role name in why_later in PF
        pass

    if rubric_case.get("expect_f1_on_best_focal"):
        # Prefer best among units that were labeled focal in decompose
        focal_scores = []
        # Our PF maps focal→primary; other units→secondary_*
        if has_focal and "primary" in link_by_role:
            focal_scores.append(float(link_by_role["primary"].get("entityness") or 0))
        if focal_scores:
            fe = max(focal_scores)
            if fe < F1_THR:
                reasons.append(f"focal_entityness_below_f1={fe:.3f}<{F1_THR}")
            if best_role and best_role != "primary" and best_ent > fe + 0.05:
                # ambient/action beat focal — only fail for clear_focal
                if rubric_case.get("objective_class") == "clear_focal":
                    reasons.append(f"non_focal_beat_focal best_role={best_role}")
        elif rubric_case.get("require_focal_unit"):
            reasons.append("no_focal_entityness_link")
    else:
        if mode == "classic_scan" or best_ent < F1_THR:
            notes.append("no_clear_anchor_expected_ok")

    # Gold stems NEVER cause fail — record coverage as info only
    gold = list(case.get("anchor_gold") or []) + list(case.get("expected_stems") or [])
    owner_stems = [str(L.get("owner_stem") or "") for L in links if isinstance(L, dict)]
    gold_touch = [g for g in gold if any(g.lower() in (o or "").lower() or (o or "").lower() in g.lower() for o in owner_stems if o)]
    if gold and not gold_touch:
        notes.append("gold_stems_absent_from_owners_na")

    obj = rubric_case.get("objective_class") or "unknown"
    # Diffuse / clear_implement without F1 expectation: only structural + grounding
    if obj in ("diffuse_implement", "clear_implement") and not rubric_case.get(
        "expect_f1_on_best_focal"
    ):
        reasons = [
            r
            for r in reasons
            if r.startswith("low_grounded")
            or r.startswith("missing_decompose")
            or r.startswith("empty_units")
            or r.startswith("bad_structure")
            or r.startswith("lexical_cue_not_projected")
        ]
        if not structural_ok:
            reasons.append("bad_structure")

    if not structural_ok and "missing_decompose_json" not in reasons:
        reasons.append("bad_structure")
    if not units:
        reasons.append("empty_units_at_stop")

    row = {
        "id": cid,
        "objective_class": obj,
        "pass": len(reasons) == 0,
        "reasons": reasons,
        "notes": notes,
        "stop_level": decomp.get("stop_level"),
        "stop_reason": decomp.get("stop_reason"),
        "n_levels": len(decomp.get("levels") or []),
        "n_units": len(units),
        "roles": roles,
        "all_terms": all_terms,
        "grounded_ratio": round(grounded_ratio, 3),
        "explore_mode": mode,
        "best_role": best_role,
        "best_entityness": best_ent,
        "entityness_entropy": round(entropy, 3),
        "unit_entityness": [
            {
                "role": L.get("role"),
                "terms": L.get("search_terms"),
                "entityness": L.get("entityness"),
                "concentration": L.get("concentration"),
                "hit_count": L.get("hit_count"),
                "owner_stem": L.get("owner_stem"),
            }
            for L in links
            if isinstance(L, dict)
        ],
        "gold_owner_touch": gold_touch,
        "mixed_ambient_focal": mixed_unit,
    }
    return row


def summarize(rows: list[dict]) -> dict:
    by_class: dict[str, dict] = {}
    for r in rows:
        c = r.get("objective_class") or "unknown"
        by_class.setdefault(c, {"n": 0, "pass": 0})
        by_class[c]["n"] += 1
        by_class[c]["pass"] += int(bool(r.get("pass")))
    return {
        "n": len(rows),
        "pass": sum(bool(r.get("pass")) for r in rows),
        "f1_anchor": sum(r.get("explore_mode") == "f1_anchor" for r in rows),
        "classic_scan": sum(r.get("explore_mode") == "classic_scan" for r in rows),
        "mean_entropy": round(
            sum(float(r.get("entityness_entropy") or 0) for r in rows) / max(1, len(rows)), 3
        ),
        "mean_grounded": round(
            sum(float(r.get("grounded_ratio") or 0) for r in rows) / max(1, len(rows)), 3
        ),
        "by_objective_class": by_class,
    }


def gate(summary: dict) -> tuple[bool, list[str]]:
    reasons = []
    if int(summary.get("n") or 0) < 1:
        reasons.append("empty")
    # Soft gate: all clear_focal must pass; diffuse may pass structural
    return len(reasons) == 0, reasons


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--round-dir", type=Path, required=True)
    ap.add_argument("--cases", type=Path, default=DEFAULT_CASES)
    ap.add_argument("--rubric", type=Path, default=DEFAULT_RUBRIC)
    ap.add_argument("--check-gate", action="store_true")
    args = ap.parse_args()

    cases = load_cases(args.cases)
    rub = load_json(args.rubric)
    rub_cases = rub.get("cases") or {}
    rows = []
    for case in cases:
        cid = case["id"]
        rows.append(score_case(case, args.round_dir / cid, rub_cases.get(cid) or {}))

    summary = summarize(rows)
    out = {
        "rows": rows,
        "summary": summary,
        "gate": {"pass": True, "reasons": []},
    }
    ok, greasons = gate(summary)
    # Gate: every clear_focal must pass
    focal_fail = [
        r["id"]
        for r in rows
        if r.get("objective_class") == "clear_focal" and not r.get("pass")
    ]
    if focal_fail:
        ok = False
        greasons.append("clear_focal_failed:" + ",".join(focal_fail))
    out["gate"] = {"pass": ok, "reasons": greasons}

    out_path = args.round_dir / "decompose_score.json"
    out_path.write_text(json.dumps(out, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print("=== Intent decompose + entityness ===\n")
    for r in rows:
        print(
            f"## {r['id']}  class={r.get('objective_class')} pass={r.get('pass')} "
            f"mode={r.get('explore_mode')} H={r.get('entityness_entropy')} "
            f"grounded={r.get('grounded_ratio')}"
        )
        print(f"  stop={r.get('stop_level')}/{r.get('stop_reason')} units={r.get('n_units')} roles={r.get('roles')}")
        print(f"  terms={r.get('all_terms')}")
        for u in r.get("unit_entityness") or []:
            print(
                f"    - [{u.get('role')}] ent={u.get('entityness')} conc={u.get('concentration')} "
                f"hits={u.get('hit_count')} stem={u.get('owner_stem')} terms={u.get('terms')}"
            )
        if r.get("reasons"):
            print(f"  FAIL: {r['reasons']}")
        if r.get("notes"):
            print(f"  notes: {r['notes']}")
        print()
    print(json.dumps({"summary": summary, "gate": out["gate"]}, indent=2, ensure_ascii=False))
    if args.check_gate and not ok:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
