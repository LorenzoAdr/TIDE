#!/usr/bin/env python3
"""Overnight zone-judge queue: epistemic + legacy ablation, score, MORNING.md.

Sequential only (one llama-server). Resumes with --start-at on partial rounds.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build/l2_harness_cli"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
BATTERY = ROOT / "tools/l2_zone_judge_battery.py"
SCORE_JUDGE = ROOT / "tools/score_zone_judge.py"
SCORE_CARDS = ROOT / "tools/l2_explore_battery/score_judge_cards.py"
KILL_RUNTIME = ROOT / "tools/l2_battery/kill_l2_runtime.py"
NIGHT = ROOT / ".tuide/ai/l2_overnight/zone_judge"
BATTERY_DIR = ROOT / ".tuide/ai/l2_explore_battery"


def log(msg: str) -> None:
    line = f"{datetime.now().isoformat(timespec='seconds')} {msg}"
    print(line, flush=True)
    NIGHT.mkdir(parents=True, exist_ok=True)
    with (NIGHT / "queue.log").open("a", encoding="utf-8") as handle:
        handle.write(line + "\n")


def load_cases() -> list[dict]:
    return json.loads(PROMPTS.read_text(encoding="utf-8"))


def kill_runtime() -> None:
    if KILL_RUNTIME.exists():
        subprocess.run(
            [sys.executable, str(KILL_RUNTIME)],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
    subprocess.run(
        ["pkill", "-f", "llama-server.*nomic-embed"],
        check=False,
        capture_output=True,
        text=True,
    )
    time.sleep(2)


def round_dir(label: str) -> Path:
    return BATTERY_DIR / f"round_{label}"


def cards_dir(cards_label: str) -> Path:
    return BATTERY_DIR / f"round_{cards_label}_cards"


def case_complete(case_dir: Path) -> bool:
    decision_path = case_dir / "decision.json"
    if not decision_path.exists():
        return False
    try:
        decision = json.loads(decision_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return False
    if not decision.get("ok"):
        return False
    return (case_dir / "cards.md").exists()


def find_resume_start(out: Path, cases: list[dict], *, resume_failed: bool) -> str:
    for case in cases:
        case_id = str(case["id"])
        case_out = out / case_id
        if resume_failed:
            if not case_complete(case_out):
                return case_id
        elif not (case_out / "decision.json").exists():
            return case_id
    return ""


def score_round(label: str, cards_label: str) -> dict:
    out = round_dir(label)
    subprocess.run(
        [sys.executable, str(SCORE_JUDGE), "--round-dir", str(out)],
        cwd=ROOT,
        check=False,
    )
    pre_out = NIGHT / f"pre_llm_{label}.json"
    subprocess.run(
        [
            sys.executable,
            str(SCORE_CARDS),
            "--cards-root",
            str(cards_dir(cards_label)),
            "--out",
            str(pre_out),
        ],
        cwd=ROOT,
        check=False,
    )
    summary_path = out / "rescored_summary.json"
    if summary_path.exists():
        return json.loads(summary_path.read_text(encoding="utf-8"))
    return {}


def run_battery(
    *,
    label: str,
    cards_label: str,
    legacy: bool,
    skip_cards: bool,
    resume: bool,
    resume_failed: bool,
    model_id: str,
) -> tuple[int, dict]:
    if not CLI.exists():
        log(f"ERROR missing {CLI}; run cmake --build build --target l2_harness_cli")
        return 1, {}

    out = round_dir(label)
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases()

    start_at = ""
    if resume:
        start_at = find_resume_start(out, cases, resume_failed=resume_failed)
        if start_at:
            log(f"resume {label} from case {start_at}")
        elif all(case_complete(out / str(c["id"])) for c in cases):
            log(f"skip {label}: all cases complete")
            metrics = score_round(label, cards_label)
            return 0, metrics

    cmd = [
        sys.executable,
        str(BATTERY),
        "--label",
        label,
        "--cards-label",
        cards_label,
    ]
    if skip_cards:
        cmd.append("--skip-cards")
    if legacy:
        cmd.append("--legacy-triage")
    if start_at:
        cmd.extend(["--start-at", start_at])
    if model_id:
        cmd.extend(["--model-id", model_id])

    log(f"START battery label={label} cards={cards_label} legacy={legacy}")
    console = NIGHT / f"{label}.console.log"
    env = os.environ.copy()
    env["TUIDE_ROOT"] = str(ROOT)
    with console.open("a", encoding="utf-8") as handle:
        handle.write(f"\n==== {datetime.now().isoformat(timespec='seconds')} {label} ====\n")
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
            text=True,
        )
    rc = proc.returncode
    log(f"END battery {label} rc={rc}")
    kill_runtime()
    metrics = score_round(label, cards_label)
    (NIGHT / f"{label}_metrics.json").write_text(
        json.dumps(metrics, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return rc, metrics


def write_morning(
    history: list[dict],
    *,
    cards_label: str,
    pre_llm: dict | None,
) -> None:
    lines = [
        "# Zone judge overnight",
        "",
        f"Generated: {datetime.now().isoformat(timespec='seconds')}",
        "",
        "## Pre-LLM (cards base)",
        "",
    ]
    if pre_llm:
        lines.append("```json")
        lines.append(json.dumps(pre_llm, ensure_ascii=False, indent=2))
        lines.append("```")
    else:
        lines.append("_missing pre-LLM score_")
    lines.extend(["", "## Runs", ""])
    for entry in history:
        metrics = entry.get("metrics") or {}
        lines.append(
            f"- **{entry.get('name', entry.get('label'))}** (`{entry.get('label')}`): "
            f"rc={entry.get('rc')} "
            f"any_hit={metrics.get('any_hit')} "
            f"available_any={metrics.get('available_any')} "
            f"anchor_hit={metrics.get('anchor_hit')} "
            f"valid={metrics.get('valid')}"
        )
        if metrics.get("miss_available_ids"):
            lines.append(f"  - miss_available: {metrics['miss_available_ids']}")
        if metrics.get("unavailable_ids"):
            lines.append(f"  - unavailable: {metrics['unavailable_ids']}")
    ep = next((h for h in history if h.get("label") == "zone_judge_recall_v1"), None)
    leg = next((h for h in history if h.get("label") == "zone_judge_recall_v1_legacy"), None)
    if ep and leg:
        em = ep.get("metrics") or {}
        lm = leg.get("metrics") or {}
        lines.extend(
            [
                "",
                "## Epistemic vs legacy",
                "",
                f"- Δ any_hit: {(em.get('any_hit') or 0) - (lm.get('any_hit') or 0)}",
                f"- Δ available_any: {(em.get('available_any') or 0) - (lm.get('available_any') or 0)}",
                f"- Δ anchor_hit: {(em.get('anchor_hit') or 0) - (lm.get('anchor_hit') or 0)}",
                "",
            ]
        )
    lines.extend(
        [
            "## Artefactos",
            "",
            f"- Log: `{NIGHT / 'queue.log'}`",
            f"- Cards: `{cards_dir(cards_label)}`",
            f"- Epistemic round: `{round_dir('zone_judge_recall_v1')}`",
            f"- Legacy round: `{round_dir('zone_judge_recall_v1_legacy')}`",
            "",
            "## Siguiente paso (humano)",
            "",
            "- Si `miss_available_ids` domina → selección post-synth / thin slice v2.",
            "- Si `unavailable_ids` domina → recall upstream (15/16, rank/pureza).",
            "",
        ]
    )
    path = NIGHT / "MORNING.md"
    path.write_text("\n".join(lines), encoding="utf-8")
    log(f"wrote {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Overnight zone-judge battery queue")
    parser.add_argument(
        "--cards-label",
        default="zone_judge_recall_v1",
        help="Shared prebuilt cards label (round_<label>_cards)",
    )
    parser.add_argument(
        "--epistemic-label",
        default="zone_judge_recall_v1",
        help="Output label for epistemic two-pass run",
    )
    parser.add_argument(
        "--legacy-label",
        default="zone_judge_recall_v1_legacy",
        help="Output label for legacy triage ablation",
    )
    parser.add_argument(
        "--build-cards",
        action="store_true",
        help="Regenerate judge cards (default: reuse round_<cards-label>_cards)",
    )
    parser.add_argument("--epistemic-only", action="store_true")
    parser.add_argument("--legacy-only", action="store_true")
    parser.add_argument("--no-legacy", action="store_true", help="Skip legacy ablation")
    parser.add_argument(
        "--resume",
        action="store_true",
        default=True,
        help="Resume partial rounds (default: on)",
    )
    parser.add_argument(
        "--no-resume",
        action="store_true",
        help="Always run full rounds from case 1",
    )
    parser.add_argument(
        "--resume-failed",
        action="store_true",
        help="Resume at first invalid/missing case (default: first missing only)",
    )
    parser.add_argument("--model-id", default="")
    args = parser.parse_args()
    resume = args.resume and not args.no_resume
    skip_cards = not args.build_cards

    NIGHT.mkdir(parents=True, exist_ok=True)
    (NIGHT / "STATUS.md").write_text(
        f"# zone judge overnight START {datetime.now().isoformat(timespec='seconds')}\n",
        encoding="utf-8",
    )
    log("=== zone judge overnight START ===")
    kill_runtime()

    pre_llm_path = NIGHT / f"pre_llm_{args.cards_label}.json"
    if cards_dir(args.cards_label).exists():
        subprocess.run(
            [
                sys.executable,
                str(SCORE_CARDS),
                "--cards-root",
                str(cards_dir(args.cards_label)),
                "--out",
                str(pre_llm_path),
                "--include-core-context",
            ],
            cwd=ROOT,
            check=False,
        )
    pre_llm = (
        json.loads(pre_llm_path.read_text(encoding="utf-8")).get("summary")
        if pre_llm_path.exists()
        else None
    )

    history: list[dict] = []
    run_epistemic = not args.legacy_only
    run_legacy = not args.epistemic_only and not args.no_legacy

    if run_epistemic:
        rc, metrics = run_battery(
            label=args.epistemic_label,
            cards_label=args.cards_label,
            legacy=False,
            skip_cards=skip_cards,
            resume=resume,
            resume_failed=args.resume_failed,
            model_id=args.model_id,
        )
        history.append(
            {
                "name": "epistemic",
                "label": args.epistemic_label,
                "rc": rc,
                "metrics": metrics,
            }
        )

    if run_legacy:
        rc, metrics = run_battery(
            label=args.legacy_label,
            cards_label=args.cards_label,
            legacy=True,
            skip_cards=skip_cards,
            resume=resume,
            resume_failed=args.resume_failed,
            model_id=args.model_id,
        )
        history.append(
            {
                "name": "legacy",
                "label": args.legacy_label,
                "rc": rc,
                "metrics": metrics,
            }
        )

    write_morning(history, cards_label=args.cards_label, pre_llm=pre_llm)
    (NIGHT / "FINISHED.txt").write_text(
        datetime.now().isoformat(timespec="seconds") + "\n",
        encoding="utf-8",
    )
    (NIGHT / "STATUS.md").write_text(
        f"# zone judge overnight FINISHED {datetime.now().isoformat(timespec='seconds')}\n"
        f"see MORNING.md\n",
        encoding="utf-8",
    )
    log("=== zone judge overnight FINISHED ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
