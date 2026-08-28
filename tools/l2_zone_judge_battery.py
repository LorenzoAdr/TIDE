#!/usr/bin/env python3
"""Build causal_judge_v1 cards, then judge all cases with one sequential L2Brain."""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/l2_harness_cli"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="zone_judge_recall_v1")
    parser.add_argument(
        "--cards-label",
        default="",
        help="Cards directory label (default: same as --label)",
    )
    parser.add_argument(
        "--maps-from",
        default=".tuide/ai/l2_explore_battery/round_registry_trails_v1",
    )
    parser.add_argument("--skip-cards", action="store_true")
    parser.add_argument("--start-at", default="")
    parser.add_argument("--only", default="")
    parser.add_argument("--one-pass", action="store_true")
    parser.add_argument("--legacy-triage", action="store_true")
    parser.add_argument(
        "--primary-survey",
        action="store_true",
        help="Epistemic contrast+must-compete (top-2 incompatible threads)",
    )
    parser.add_argument(
        "--slot-survey",
        action="store_true",
        help="Hypothesis-per-slot (1 card/pass) → retain 2–3 hyps; no rival contrast",
    )
    parser.add_argument("--model-id", default="")
    args = parser.parse_args()

    cases = json.loads(PROMPTS.read_text(encoding="utf-8"))
    cards_label = args.cards_label or args.label
    cards_root = (
        ROOT / ".tuide" / "ai" / "l2_explore_battery" / f"round_{cards_label}_cards"
    )
    judge_root = (
        ROOT / ".tuide" / "ai" / "l2_explore_battery" / f"round_{args.label}"
    )
    maps_root = Path(args.maps_from)
    if not maps_root.is_absolute():
        maps_root = ROOT / maps_root
    cards_root.mkdir(parents=True, exist_ok=True)
    judge_root.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env["TUIDE_ROOT"] = str(ROOT)
    started = not args.start_at
    for case in cases:
        case_id = str(case["id"])
        if args.only and case_id != args.only:
            continue
        if not started:
            started = case_id == args.start_at
        if not started:
            continue
        case_dir = cards_root / case_id
        case_dir.mkdir(parents=True, exist_ok=True)
        cards_path = case_dir / "judge_cards.md"
        payload_path = case_dir / "judge_cards.json"
        if args.skip_cards and cards_path.exists() and (
            args.one_pass or payload_path.exists()
        ):
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
            str(case["prompt"]),
        ]
        print(f"==== cards {case_id} ====", flush=True)
        result = subprocess.run(
            command,
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            timeout=300,
        )
        (case_dir / "cards.log").write_text(
            (result.stderr or "") + "\n" + (result.stdout or ""),
            encoding="utf-8",
        )
        if result.returncode != 0 or not result.stdout.startswith("# causal_judge_v1"):
            print(f"FAIL cards {case_id}: rc={result.returncode}", flush=True)
            continue
        cards_path.write_text(result.stdout, encoding="utf-8")

    command = [
        str(CLI),
        "zone-judge-battery",
        "--cards-root",
        str(cards_root),
        "--out",
        str(judge_root),
    ]
    if args.start_at:
        command += ["--start-at", args.start_at]
    if args.only:
        command += ["--only", args.only]
    if not args.one_pass:
        command.append("--two-pass")
    if args.legacy_triage:
        command.append("--legacy-triage")
    if args.primary_survey:
        command.append("--primary-survey")
    if args.slot_survey:
        command.append("--slot-survey")
    if args.model_id:
        command += ["--model-id", args.model_id]
    print("==== sequential zone judge ====", flush=True)
    return subprocess.call(command, cwd=ROOT, env=env)


if __name__ == "__main__":
    raise SystemExit(main())
