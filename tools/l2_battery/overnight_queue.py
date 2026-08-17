#!/usr/bin/env python3
"""Overnight L2 queue: wait hard2 → score → branch → MORNING.md.

Branches (after hard2):
  A) clear win (all_pass>=1 or recall>=0.60) → 1× hard repeat → stop
  B) multi ok-ish, sibling weak → sibling-only × N → stop
  C) no improvement vs Phase A baseline → stop with report
"""
from __future__ import annotations

import importlib.util
import json
import os
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = ROOT / "build" / "l2_harness_cli"
CASES_HARD = ROOT / "tools/l2_battery/prompt_packs/cases_hard.json"
CASES_SIBLING = ROOT / "tools/l2_battery/prompt_packs/cases_sibling_only.json"
HARD2 = ROOT / ".tuide/ai/l2_phase_e_hard2"
NIGHT = ROOT / ".tuide/ai/l2_overnight"
BASELINE_RECALL = 0.49  # Phase A hard mean
BASELINE_ALL_PASS = 0
CASE_TIMEOUT = 2700
SIBLING_REPS = 3
WIN_RECALL = 0.60


def log(msg: str) -> None:
    line = f"{datetime.now().isoformat(timespec='seconds')} {msg}"
    print(line, flush=True)
    NIGHT.mkdir(parents=True, exist_ok=True)
    with (NIGHT / "queue.log").open("a", encoding="utf-8") as f:
        f.write(line + "\n")


def load_score_mod():
    spec = importlib.util.spec_from_file_location(
        "score_facets", ROOT / "tools/l2_battery/score_facets.py"
    )
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def score_round(cases_path: Path, round_dir: Path) -> dict:
    sf = load_score_mod()
    # Prefer writing metrics.json via CLI for persistence
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "tools/l2_battery/score_facets.py"),
            "--cases",
            str(cases_path),
            "--round-dir",
            str(round_dir),
        ],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    metrics_path = round_dir / "metrics.json"
    if metrics_path.exists():
        return json.loads(metrics_path.read_text(encoding="utf-8"))
    # Fallback inline
    cases = sf.load_cases(cases_path)
    rows = {}
    results = round_dir / "results.jsonl"
    if results.exists():
        for line in results.read_text(encoding="utf-8").splitlines():
            if line.strip():
                r = json.loads(line)
                rows[r["id"]] = r
    recalls = []
    passes = 0
    case_rows = []
    for c in cases:
        cid = c["id"]
        if cid not in rows:
            case_rows.append({"id": cid, "missing": True, "all_pass": False, "facet_recall": 0.0})
            continue
        sc = sf.score_case(c, round_dir / cid, rows[cid])
        recalls.append(sc["facet_recall"])
        if sc["all_pass"]:
            passes += 1
        case_rows.append(sc)
    mean = sum(recalls) / len(recalls) if recalls else 0.0
    return {
        "facet_recall": mean,
        "all_pass": passes,
        "all_pass_rate": passes / len(cases) if cases else 0.0,
        "n_cases": len(cases),
        "cases": case_rows,
    }


def wait_finished(round_dir: Path, label: str, poll_s: int = 45) -> bool:
    fin = round_dir / "FINISHED.txt"
    log(f"wait {label} → {fin}")
    needle = round_dir.name
    dead_streak = 0
    while not fin.exists():
        running2 = subprocess.run(
            ["pgrep", "-af", "run_closed_loop_round.py"],
            capture_output=True,
            text=True,
        )
        alive = any(
            needle in ln and "overnight_queue" not in ln
            for ln in (running2.stdout or "").splitlines()
        )
        pid_file = round_dir / "runner.pid"
        if not alive and pid_file.exists():
            try:
                pid = int(pid_file.read_text().strip().split()[0])
                os.kill(pid, 0)
                alive = True
            except (ValueError, OSError, ProcessLookupError):
                pass
        # Also treat harness as alive (hard2 may be mid-case)
        if not alive:
            h = subprocess.run(
                ["pgrep", "-af", "l2_harness_cli run"],
                capture_output=True,
                text=True,
            )
            alive = bool((h.stdout or "").strip())
        if alive:
            dead_streak = 0
        else:
            dead_streak += 1
            log(f"WARN: {label} runner not seen ({dead_streak}/3)")
            if dead_streak >= 3 and not fin.exists():
                log(f"WARN: giving up wait on {label}")
                return False
        time.sleep(poll_s)
    log(f"done {label}: {fin.read_text().strip()}")
    return True


def run_round(cases: Path, out: Path, label: str) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    log(f"START {label} → {out}")
    env = os.environ.copy()
    env["L2_PROMPT_PACK"] = str(
        ROOT / "tools/l2_battery/prompt_packs/t_sibling_guide.json"
    )
    env["L2_FEAT_POST_EDIT_COVERAGE"] = "1"
    # Restore product before round
    subprocess.run(
        ["git", "checkout", "--", "src/ui", "src/util"],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    console = out / "console.log"
    with console.open("a", encoding="utf-8") as cf:
        proc = subprocess.Popen(
            [
                sys.executable,
                str(ROOT / "tools/l2_battery/run_closed_loop_round.py"),
                "--cases",
                str(cases),
                "--out",
                str(out),
                "--cli",
                str(CLI),
                "--root",
                str(ROOT),
                "--case-timeout",
                str(CASE_TIMEOUT),
            ],
            cwd=ROOT,
            env=env,
            stdout=cf,
            stderr=subprocess.STDOUT,
        )
        (out / "runner.pid").write_text(str(proc.pid) + "\n")
        rc = proc.wait()
    log(f"END {label} rc={rc}")
    metrics = score_round(cases, out)
    (out / "metrics_branch.json").write_text(
        json.dumps(metrics, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    return metrics


def case_pass_map(metrics: dict) -> dict[str, bool]:
    out = {}
    for c in metrics.get("cases") or []:
        cid = c.get("id")
        if not cid:
            continue
        out[cid] = bool(c.get("all_pass"))
    return out


def decide_branch(metrics: dict) -> str:
    recall = float(metrics.get("facet_recall") or 0.0)
    all_pass = int(metrics.get("all_pass") or 0)
    pm = case_pass_map(metrics)
    multi_ok = pm.get("multi_two_markers") or pm.get("multi_ktab_helper")
    sibling_pass = int(pm.get("sibling_always_true", False)) + int(
        pm.get("sibling_noop", False)
    )

    if all_pass >= 1 or recall >= WIN_RECALL:
        return "A_win_repeat_hard"
    # multi progressed, siblings still weak
    if (multi_ok or recall > BASELINE_RECALL + 0.05) and sibling_pass == 0:
        return "B_sibling_reps"
    if recall <= BASELINE_RECALL + 0.02 and all_pass <= BASELINE_ALL_PASS:
        return "C_no_gain_stop"
    # mild improvement but no all_pass — still probe siblings
    if sibling_pass == 0:
        return "B_sibling_reps"
    return "A_win_repeat_hard"


def write_morning(history: list[dict], branch: str, hard2: dict) -> None:
    lines = []
    lines.append("# L2 overnight — MORNING")
    lines.append("")
    lines.append(f"Generated: {datetime.now().isoformat(timespec='seconds')}")
    lines.append(f"Branch taken: `{branch}`")
    lines.append("")
    lines.append("## Baseline (Phase A hard)")
    lines.append(f"- recall ≈ {BASELINE_RECALL}, all_pass = {BASELINE_ALL_PASS}/4")
    lines.append("")
    lines.append("## Smoke E (kept)")
    lines.append("- multi_two_markers PASS recall=1.0; sibling_always_true fail 0.17")
    lines.append("")
    lines.append("## Hard2 (edit-apply MVP)")
    lines.append(
        f"- recall={hard2.get('facet_recall')} all_pass={hard2.get('all_pass')}/{hard2.get('n_cases')}"
    )
    for c in hard2.get("cases") or []:
        miss = [
            f.get("name")
            for f in (c.get("facets") or [])
            if not f.get("ok")
        ]
        lines.append(
            f"  - {c.get('id')}: {'PASS' if c.get('all_pass') else 'fail'} "
            f"recall={c.get('facet_recall')} miss={miss}"
        )
    lines.append("")
    lines.append("## Queue history")
    for h in history:
        lines.append(
            f"- {h.get('label')}: recall={h.get('facet_recall')} "
            f"all_pass={h.get('all_pass')} out={h.get('out')}"
        )
    lines.append("")
    lines.append("## Suggested next (human)")
    if branch.startswith("A"):
        lines.append("- Edit-apply MVP looks promising; consider promoting flex match as default.")
        lines.append("- Optional: resume prompt sweep only for sibling-focused packs.")
    elif branch.startswith("B"):
        lines.append("- Flex helped multi; sibling still weak → next: shape/sibling_of runtime, not more packs.")
    else:
        lines.append("- No clear gain vs Phase A → inspect hard2 sessions before more battery.")
    lines.append("")
    path = NIGHT / "MORNING.md"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # Also mirror to checkpoint
    ck = ROOT / ".tuide/ai/l2_prompt_sweep_cont2/CHECKPOINT.md"
    if ck.exists():
        with ck.open("a", encoding="utf-8") as f:
            f.write(f"\n## Overnight finished ({datetime.now().isoformat(timespec='seconds')})\n")
            f.write(f"- See `{path}`\n")
            f.write(f"- Branch: {branch}\n")
    log(f"wrote {path}")


def main() -> int:
    NIGHT.mkdir(parents=True, exist_ok=True)
    (NIGHT / "STATUS.md").write_text(
        f"# overnight queue started {datetime.now().isoformat(timespec='seconds')}\n",
        encoding="utf-8",
    )
    history: list[dict] = []

    # Ensure sibling-only pack exists
    if not CASES_SIBLING.exists():
        hard = json.loads(CASES_HARD.read_text(encoding="utf-8"))
        sib = [c for c in hard if "sibling" in c["id"]]
        CASES_SIBLING.write_text(
            json.dumps(sib, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )

    log("=== phase: wait hard2 ===")
    if not (HARD2 / "FINISHED.txt").exists():
        ok = wait_finished(HARD2, "hard2", poll_s=45)
        if not ok and not (HARD2 / "FINISHED.txt").exists():
            # If results incomplete but runner gone, still score what we have
            log("hard2 incomplete; scoring partial if any")
    hard2 = score_round(CASES_HARD, HARD2)
    history.append(
        {
            "label": "hard2",
            "out": str(HARD2),
            "facet_recall": hard2.get("facet_recall"),
            "all_pass": hard2.get("all_pass"),
        }
    )
    (NIGHT / "hard2_metrics.json").write_text(
        json.dumps(hard2, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    branch = decide_branch(hard2)
    log(f"branch={branch} recall={hard2.get('facet_recall')} all_pass={hard2.get('all_pass')}")
    (NIGHT / "branch.txt").write_text(branch + "\n", encoding="utf-8")

    if branch == "A_win_repeat_hard":
        out = NIGHT / "hard_repeat"
        m = run_round(CASES_HARD, out, "hard_repeat")
        history.append(
            {
                "label": "hard_repeat",
                "out": str(out),
                "facet_recall": m.get("facet_recall"),
                "all_pass": m.get("all_pass"),
            }
        )
    elif branch == "B_sibling_reps":
        for i in range(1, SIBLING_REPS + 1):
            out = NIGHT / f"sibling_rep_{i}"
            m = run_round(CASES_SIBLING, out, f"sibling_rep_{i}")
            history.append(
                {
                    "label": f"sibling_rep_{i}",
                    "out": str(out),
                    "facet_recall": m.get("facet_recall"),
                    "all_pass": m.get("all_pass"),
                }
            )
            # Early stop if both siblings pass once
            if int(m.get("all_pass") or 0) >= 2:
                log("sibling both PASS — early stop reps")
                break
    else:
        log("C_no_gain_stop — no further rounds")

    write_morning(history, branch, hard2)
    (NIGHT / "FINISHED.txt").write_text(
        datetime.now().isoformat(timespec="seconds") + "\n", encoding="utf-8"
    )
    (NIGHT / "STATUS.md").write_text(
        f"# overnight queue FINISHED {datetime.now().isoformat(timespec='seconds')}\n"
        f"branch={branch}\nsee MORNING.md\n",
        encoding="utf-8",
    )
    log("=== overnight queue complete ===")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
