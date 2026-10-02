#!/usr/bin/env python3
"""Score mechanism-pack quality from causal_judge_v1 JSON (generic, no edge golds)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"

STEM_RE = re.compile(r"[A-Za-z0-9_]+")


def stem_hits(text: str, stems: list[str]) -> set[str]:
    if not text or not stems:
        return set()
    low = text.lower()
    hit = set()
    for stem in stems:
        s = stem.lower()
        if s and s in low:
            hit.add(stem)
    return hit


def edge_blob(edge: dict) -> str:
    parts = [
        str(edge.get("from", "")),
        str(edge.get("to", "")),
        str(edge.get("member", "")),
        str(edge.get("kind", "")),
        str(edge.get("from_zone", "")),
        str(edge.get("to_zone", "")),
    ]
    return " ".join(parts)


def mechanism_edges(zone: dict) -> list[dict]:
    mech = zone.get("mechanism") or {}
    out = []
    for slot in ("trigger", "state", "effect"):
        edge = mech.get(slot)
        if isinstance(edge, dict):
            e = dict(edge)
            e["_slot"] = slot
            out.append(e)
    return out


def score_payload(payload: dict, expected: list[str], traps: list[str]) -> dict:
    zones = payload.get("zones") or []
    bridges = payload.get("zone_bridges") or []
    slots_filled = 0
    slots_possible = 0
    skel_gold = set()
    skel_trap = set()
    support_gold = 0
    support_trap = 0
    support_n = 0
    budget_waste = 0
    ports_nonempty = 0
    zones_with_bridge_opportunity = 0
    story_gold = set()
    zone_ids_in_bridges: set[str] = set()
    for bridge in bridges:
        for z in bridge.get("zones") or []:
            zone_ids_in_bridges.add(str(z))

    for zone in zones:
        if not isinstance(zone, dict):
            continue
        zid = str(zone.get("id", ""))
        mech_edges = mechanism_edges(zone)
        meta = zone.get("pack_meta") or {}
        missing = meta.get("skeleton_missing") or []
        for slot in ("trigger", "state", "effect"):
            slots_possible += 1
            filled = any(e.get("_slot") == slot for e in mech_edges)
            if filled:
                slots_filled += 1
            elif slot not in missing and meta.get("mechanism_pack"):
                # cup may be 0; still count possible only when pack on
                pass
        for edge in mech_edges:
            blob = edge_blob(edge)
            skel_gold |= stem_hits(blob, expected)
            skel_trap |= stem_hits(blob, traps)
            story_gold |= stem_hits(blob, expected)
        ports = zone.get("ports") or []
        if ports:
            ports_nonempty += 1
        if zid in zone_ids_in_bridges:
            zones_with_bridge_opportunity += 1
        for edge in ports:
            blob = edge_blob(edge)
            story_gold |= stem_hits(blob, expected)
            skel_trap |= stem_hits(blob, traps)
        for edge in zone.get("support_edges") or []:
            support_n += 1
            blob = edge_blob(edge)
            g = stem_hits(blob, expected)
            t = stem_hits(blob, traps)
            if g:
                support_gold += 1
            if t and not g:
                support_trap += 1
            kind = str(edge.get("kind", ""))
            # crude waste: call without gold touch
            if kind == "call" and not g:
                budget_waste += 1
            member = str(edge.get("member", ""))
            if member and any(member == str(m.get("member", "")) for m in mech_edges):
                budget_waste += 1

    skel_slot_fill = (slots_filled / slots_possible) if slots_possible else 0.0
    support_gold_density = (support_gold / support_n) if support_n else 0.0
    n_zones = len(zones) or 1
    # Prefer coverage among bridge-touched zones; else among all zones.
    denom = zones_with_bridge_opportunity if zones_with_bridge_opportunity else n_zones
    port_present = min(1.0, ports_nonempty / denom) if denom else 0.0
    return {
        "skel_gold_touch": bool(skel_gold),
        "skel_gold_stems": sorted(skel_gold),
        "skel_slot_fill": skel_slot_fill,
        "slots_filled": slots_filled,
        "slots_possible": slots_possible,
        "support_gold_density": support_gold_density,
        "support_n": support_n,
        "support_trap_n": support_trap,
        "trap_in_mechanism": bool(skel_trap),
        "trap_stems_in_mechanism": sorted(skel_trap),
        "budget_waste": budget_waste,
        "port_present": port_present,
        "ports_nonempty_zones": ports_nonempty,
        "bridge_zones": zones_with_bridge_opportunity,
        "story_cover_proxy": bool(story_gold),
        "story_gold_stems": sorted(story_gold),
        "n_zones": len(zones),
        "n_bridges": len(bridges),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cards-root", required=True)
    parser.add_argument("--out", default="")
    parser.add_argument("--baseline", default="", help="optional prior score JSON for deltas")
    args = parser.parse_args()
    cards_root = Path(args.cards_root)
    if not cards_root.is_absolute():
        cards_root = ROOT / cards_root
    cases = json.loads(PROMPTS.read_text(encoding="utf-8"))
    baseline_by_id: dict = {}
    if args.baseline:
        base_path = Path(args.baseline)
        if not base_path.is_absolute():
            base_path = ROOT / base_path
        if base_path.exists():
            baseline_by_id = {
                row["id"]: row
                for row in json.loads(base_path.read_text(encoding="utf-8")).get("rows", [])
                if row.get("ok")
            }

    rows = []
    for case in cases:
        case_id = str(case["id"])
        payload_path = cards_root / case_id / "judge_cards.json"
        expected = [str(s) for s in case.get("expected_stems") or []]
        traps = [str(s) for s in case.get("trap_stems") or []]
        if not payload_path.exists():
            rows.append({"id": case_id, "ok": False, "error": "missing_payload"})
            continue
        try:
            payload = json.loads(payload_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as ex:
            rows.append({"id": case_id, "ok": False, "error": f"bad_json:{ex}"})
            continue
        metrics = score_payload(payload, expected, traps)
        row = {"id": case_id, "ok": True, **metrics}
        if case_id in baseline_by_id:
            b = baseline_by_id[case_id]
            row["delta_story_cover"] = int(metrics["story_cover_proxy"]) - int(
                bool(b.get("story_cover_proxy"))
            )
            row["delta_skel_gold"] = int(metrics["skel_gold_touch"]) - int(
                bool(b.get("skel_gold_touch"))
            )
            row["delta_slot_fill"] = metrics["skel_slot_fill"] - float(b.get("skel_slot_fill") or 0)
        rows.append(row)

    valid = [r for r in rows if r.get("ok")]
    n = len(valid) or 1
    summary = {
        "total": len(rows),
        "valid": len(valid),
        "skel_gold_touch": sum(bool(r.get("skel_gold_touch")) for r in valid),
        "skel_gold_touch_rate": sum(bool(r.get("skel_gold_touch")) for r in valid) / n,
        "skel_slot_fill_mean": sum(float(r.get("skel_slot_fill") or 0) for r in valid) / n,
        "support_gold_density_mean": sum(float(r.get("support_gold_density") or 0) for r in valid)
        / n,
        "trap_in_mechanism": sum(bool(r.get("trap_in_mechanism")) for r in valid),
        "budget_waste_sum": sum(int(r.get("budget_waste") or 0) for r in valid),
        "port_present_mean": sum(float(r.get("port_present") or 0) for r in valid) / n,
        "story_cover_proxy": sum(bool(r.get("story_cover_proxy")) for r in valid),
        "story_cover_proxy_rate": sum(bool(r.get("story_cover_proxy")) for r in valid) / n,
        "missing": [r["id"] for r in rows if not r.get("ok")],
    }
    output = {"summary": summary, "rows": rows}
    text = json.dumps(output, ensure_ascii=False, indent=2) + "\n"
    if args.out:
        out = Path(args.out)
        if not out.is_absolute():
            out = ROOT / out
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
