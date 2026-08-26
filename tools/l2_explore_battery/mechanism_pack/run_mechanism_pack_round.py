#!/usr/bin/env python3
"""Build mechanism-pack cards + dossiers for a calibration round tN."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CLI = ROOT / "build/l2_harness_cli"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
PACK_DIR = Path(__file__).resolve().parent


def default_knobs(round_id: str) -> dict:
    """Global knobs only — no per-case branches."""
    base = {
        "mechanism_pack": True,
        "max_zones": 8,
        "promote_uncovered": True,
        "max_edges": 12,
        "max_representatives": 5,
        "skel_trigger_cup": 1,
        "skel_state_cup": 1,
        "skel_effect_cup": 1,
        "port_cup": 2,
        "w_kind_write": 100.0,
        "w_kind_read": 100.0,
        "w_kind_handoff": 90.0,
        "w_kind_ctrl": 80.0,
        "w_kind_call": 60.0,
        "w_kind_enter_ctrl": 50.0,
        "w_cos": 55.0,
        "w_ppr": 45.0,
        "w_anchor": 70.0,
        "w_direct": 20.0,
        "w_hub": 25.0,
        "w_redundancy": 35.0,
        "semantic_hard_floor": 45.0,
    }
    if round_id == "t0":
        # First measurable pack baseline (pre-semantic bump); kept for history.
        base = dict(base)
        base["w_cos"] = 40.0
        base["w_ppr"] = 30.0
        base["semantic_hard_floor"] = 40.0
        return base
    if round_id == "t0_off":
        base = dict(base)
        base["mechanism_pack"] = False
        return base
    if round_id == "t1":
        # Structure-dominant: lower semantic weights.
        base = dict(base)
        base["w_cos"] = 15.0
        base["w_ppr"] = 15.0
        base["semantic_hard_floor"] = 30.0
        return base
    if round_id == "t2":
        # Frozen equilibrium defaults (also compiled into RegistryCausalJudgeOpts).
        return base
    if round_id == "t3":
        # Stronger anti-redundancy + tighter ports — reverted (hurt density/cover).
        base = dict(base)
        base["w_redundancy"] = 55.0
        base["port_cup"] = 1
        base["w_hub"] = 40.0
        return base
    return base


def write_dossier(case_dir: Path, case: dict, payload: dict, baseline_metrics: dict | None) -> None:
    zones = payload.get("zones") or []
    lines = [
        f"# Dossier {case['id']}",
        "",
        f"prompt: {case.get('prompt', '')}",
        f"expected_stems: {', '.join(case.get('expected_stems') or [])}",
        f"trap_stems: {', '.join(case.get('trap_stems') or [])}",
        "",
    ]
    for zone in zones:
        if not isinstance(zone, dict):
            continue
        lines.append(f"## {zone.get('id', '?')} score={zone.get('score', 0)}")
        lines.append(f"primary_stems: {', '.join(zone.get('primary_stems') or [])}")
        mech = zone.get("mechanism") or {}
        lines.append("mechanism:")
        for slot in ("trigger", "state", "effect"):
            edge = mech.get(slot)
            if isinstance(edge, dict):
                member = f"({edge.get('member')})" if edge.get("member") else ""
                lines.append(
                    f"  - {slot}: {edge.get('from')} -{edge.get('kind')}{member}-> {edge.get('to')}"
                )
            else:
                lines.append(f"  - {slot}: (missing)")
        meta = zone.get("pack_meta") or {}
        if meta.get("skeleton_missing"):
            lines.append(f"skeleton_missing: {', '.join(meta['skeleton_missing'])}")
        ports = zone.get("ports") or []
        if ports:
            lines.append("ports:")
            for edge in ports:
                lines.append(
                    f"  - {edge.get('from_zone')}=>{edge.get('to_zone')} "
                    f"{edge.get('from')} -{edge.get('kind')}-> {edge.get('to')}"
                )
        support = zone.get("support_edges") or []
        lines.append(f"support_edges ({len(support)}):")
        for edge in support[:8]:
            member = f"({edge.get('member')})" if edge.get("member") else ""
            lines.append(
                f"  - {edge.get('from')} -{edge.get('kind')}{member}-> {edge.get('to')}"
            )
        risks = zone.get("risks") or []
        if risks:
            lines.append("risks: " + " ".join(str(r) for r in risks))
        lines.append("")
    bridges = payload.get("zone_bridges") or []
    if bridges:
        lines.append("## zone bridges")
        for bridge in bridges:
            zs = " ".join(str(z) for z in bridge.get("zones") or [])
            lines.append(f"- {bridge.get('trail')}: {zs} | {bridge.get('why', '')}")
        lines.append("")
    if baseline_metrics:
        lines.append("## delta vs baseline metrics")
        for key in (
            "skel_gold_touch",
            "skel_slot_fill",
            "story_cover_proxy",
            "trap_in_mechanism",
            "support_gold_density",
            "port_present",
            "budget_waste",
        ):
            lines.append(f"- {key}: {baseline_metrics.get(key)}")
        lines.append("")
    (case_dir / "dossier.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (case_dir / "dossier.json").write_text(
        json.dumps(
            {
                "id": case["id"],
                "prompt": case.get("prompt"),
                "expected_stems": case.get("expected_stems"),
                "trap_stems": case.get("trap_stems"),
                "zones_pack": [
                    {
                        "id": z.get("id"),
                        "mechanism": z.get("mechanism"),
                        "ports": z.get("ports"),
                        "support_edges": z.get("support_edges"),
                        "pack_meta": z.get("pack_meta"),
                        "primary_stems": z.get("primary_stems"),
                        "risks": z.get("risks"),
                    }
                    for z in zones
                    if isinstance(z, dict)
                ],
                "zone_bridges": bridges,
            },
            ensure_ascii=False,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--round", default="t0", help="round id e.g. t0, t1, t0_off")
    parser.add_argument(
        "--maps-from",
        default=".tuide/ai/l2_explore_battery/round_registry_trails_v1",
    )
    parser.add_argument("--knobs", default="", help="override knobs JSON path")
    parser.add_argument("--only", default="")
    parser.add_argument("--skip-cards", action="store_true")
    parser.add_argument(
        "--baseline-scores",
        default="",
        help="scores JSON from prior round for dossier deltas",
    )
    args = parser.parse_args()

    if not CLI.exists():
        print(f"FAIL: missing {CLI}; build l2_harness_cli first", flush=True)
        return 1

    round_id = args.round
    round_root = (
        ROOT / ".tuide" / "ai" / "l2_explore_battery" / f"round_mech_pack_{round_id}"
    )
    cards_root = round_root / "cards"
    cards_root.mkdir(parents=True, exist_ok=True)

    if args.knobs:
        knobs_path = Path(args.knobs)
        if not knobs_path.is_absolute():
            knobs_path = ROOT / knobs_path
        knobs = json.loads(knobs_path.read_text(encoding="utf-8"))
    else:
        knobs = default_knobs(round_id)
    knobs_out = PACK_DIR / f"knobs_{round_id}.json"
    knobs_out.write_text(json.dumps(knobs, indent=2) + "\n", encoding="utf-8")
    (round_root / "knobs.json").write_text(json.dumps(knobs, indent=2) + "\n", encoding="utf-8")

    maps_root = Path(args.maps_from)
    if not maps_root.is_absolute():
        maps_root = ROOT / maps_root

    baseline_by_id: dict = {}
    if args.baseline_scores:
        bp = Path(args.baseline_scores)
        if not bp.is_absolute():
            bp = ROOT / bp
        if bp.exists():
            baseline_by_id = {
                r["id"]: r
                for r in json.loads(bp.read_text(encoding="utf-8")).get("rows", [])
                if r.get("ok")
            }

    cases = json.loads(PROMPTS.read_text(encoding="utf-8"))
    env = os.environ.copy()
    env["TUIDE_ROOT"] = str(ROOT)

    for case in cases:
        case_id = str(case["id"])
        if args.only and case_id != args.only:
            continue
        case_dir = cards_root / case_id
        case_dir.mkdir(parents=True, exist_ok=True)
        payload_path = case_dir / "judge_cards.json"
        md_path = case_dir / "judge_cards.md"
        if args.skip_cards and payload_path.exists() and md_path.exists():
            payload = json.loads(payload_path.read_text(encoding="utf-8"))
            write_dossier(case_dir, case, payload, baseline_by_id.get(case_id))
            continue
        map_path = maps_root / case_id / "map_last.md"
        if not map_path.exists():
            print(f"FAIL cards {case_id}: missing map {map_path}", flush=True)
            continue
        command = [
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
            "--judge-knobs",
            str(round_root / "knobs.json"),
            str(case["prompt"]),
        ]
        print(f"==== cards {case_id} ({round_id}) ====", flush=True)
        result = subprocess.run(
            command,
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
        )
        (case_dir / "cards.log").write_text(
            (result.stdout or "") + (result.stderr or ""), encoding="utf-8"
        )
        if result.returncode != 0:
            print(f"FAIL cards {case_id}: rc={result.returncode}", flush=True)
            continue
        md_path.write_text(result.stdout or "", encoding="utf-8")
        if not payload_path.exists():
            print(f"FAIL cards {case_id}: no json", flush=True)
            continue
        payload = json.loads(payload_path.read_text(encoding="utf-8"))
        write_dossier(case_dir, case, payload, baseline_by_id.get(case_id))

    score_script = PACK_DIR / "score_mechanism_pack.py"
    scores_out = round_root / "scores.json"
    score_cmd = [
        sys.executable,
        str(score_script),
        "--cards-root",
        str(cards_root),
        "--out",
        str(scores_out),
    ]
    if args.baseline_scores:
        score_cmd.extend(["--baseline", args.baseline_scores])
    subprocess.run(score_cmd, cwd=str(ROOT), check=False)
    print(f"round={round_id} root={round_root}", flush=True)
    print(f"knobs={knobs_out}", flush=True)
    print(f"scores={scores_out}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
