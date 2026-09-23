#!/usr/bin/env python3
"""Autosupervised tool eval: one lever at a time on lite floor (thinking OFF).

Suelo: grep,read. Keep lever if improves/fixes without regression and wall≤1.5×.
Already-decided rounds under TOOL_EVAL/R*_*/DECISION.json are reused.
"""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_API = "http://192.168.64.1:8080/v1"
DEFAULT_MODEL = "qwen3.6-27b-q8_0"
LITE = ROOT / "tools/l2_wave/explore_lite_local.py"
SEED = ROOT / ".tuide/ai/l2_wave/v2_night/H_LITE_T2_28B"

# Order: prior results first, then remaining wave-ish tools.
LEVERS: list[tuple[str, str, list[str]]] = [
    # (round_dir, lever_name, extra_tools)
    ("R1_follow", "follow", ["follow"]),
    ("R2_entre", "entre", ["entre"]),
    ("R3_peek", "peek", ["peek"]),
    ("R4_needles", "needles", ["needles"]),
    ("R5_cerca", "cerca", ["cerca"]),
    ("R6_atlas", "atlas", ["atlas"]),
    ("R7_inbody", "inbody", ["inbody"]),
]


def load_summary(p: Path) -> dict:
    return json.loads(p.read_text(encoding="utf-8"))


def seed_r0(base: Path) -> dict:
    r0 = base / "R0_lite"
    r0.mkdir(parents=True, exist_ok=True)
    src_sum = SEED / "H_LITE_T2_SUMMARY.json"
    if not src_sum.is_file():
        raise SystemExit(f"missing seed summary {src_sum}")
    summary = load_summary(src_sum)
    case_secs = {
        "11_restore_session_state": 346.0,
        "13_lsp_auto_restart": 86.0,
        "20_cancel_ai_generation": 394.0,
        "18_compile_error_gutter_mark": 143.0,
        "99_cursor_ballistics_k8s": 11.0,
    }
    wall = sum(case_secs.values())
    summary = {
        **summary,
        "label": "R0_lite",
        "thinking": False,
        "tools": ["grep", "read"],
        "wall_sec": wall,
        "case_secs": case_secs,
        "seeded_from": str(SEED),
    }
    src_rep = SEED / "rep_1"
    dst_rep = r0 / "rep_1"
    if src_rep.is_dir() and not dst_rep.exists():
        shutil.copytree(src_rep, dst_rep)
    for name in ("R0_lite_SUMMARY.json", "H_LITE_T2_SUMMARY.json"):
        (r0 / name).write_text(
            json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    (r0 / "STARTED.txt").write_text(
        f"R0_lite seeded thinking=off model={summary.get('model')}\n", encoding="utf-8"
    )
    (r0 / "DONE").write_text(time.strftime("%Y-%m-%dT%H:%M:%S%z") + "\n", encoding="utf-8")
    return summary


def run_round(
    base: Path,
    name: str,
    tools: list[str],
    api: str,
    model: str,
) -> dict:
    out_parent = base / name
    out_parent.mkdir(parents=True, exist_ok=True)
    rep = out_parent / "rep_1"
    sum_path = out_parent / f"{name}_SUMMARY.json"
    if (out_parent / "DONE").is_file() and sum_path.is_file():
        print(f"skip run {name} (DONE)", flush=True)
        return load_summary(sum_path)
    # wipe partial rep if previous crash
    if rep.exists() and not sum_path.is_file():
        shutil.rmtree(rep, ignore_errors=True)
    cmd = [
        "python3",
        str(LITE),
        "--api",
        api,
        "--model",
        model,
        "--out",
        str(rep),
        "--tools",
        ",".join(tools),
        "--label",
        name,
    ]
    print("RUN " + " ".join(cmd), flush=True)
    log = out_parent / "queue.log"
    with log.open("a", encoding="utf-8") as f:
        proc = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    if proc.returncode != 0:
        raise SystemExit(f"{name} failed exit={proc.returncode}; see {log}")
    if not sum_path.is_file():
        alt = out_parent / "H_LITE_T2_SUMMARY.json"
        if alt.is_file():
            sum_path = alt
    return load_summary(sum_path)


def decide(prev: dict, cur: dict, lever: str, time_cap: float = 1.5) -> dict:
    pog = float(prev.get("ok_gold_frac") or 0)
    cog = float(cur.get("ok_gold_frac") or 0)
    pok = float(prev.get("ok_frac") or 0)
    cok = float(cur.get("ok_frac") or 0)
    pwall = float(prev.get("wall_sec") or 1)
    cwall = float(cur.get("wall_sec") or pwall)
    time_ok = cwall <= time_cap * pwall + 1e-6
    score_ok = cog >= pog - 0.05
    improved = cog > pog + 0.05 or cok > pok + 0.05
    prev_fail = {r["case"] for r in (prev.get("rows") or []) if not r.get("ok_gold")}
    cur_ok = {r["case"] for r in (cur.get("rows") or []) if r.get("ok_gold")}
    fixed = sorted(prev_fail & cur_ok)
    prev_ok = {r["case"] for r in (prev.get("rows") or []) if r.get("ok_gold")}
    cur_fail = {r["case"] for r in (cur.get("rows") or []) if not r.get("ok_gold")}
    regressed = sorted(prev_ok & cur_fail)
    keep = bool(score_ok and time_ok and (improved or fixed) and not regressed)
    if score_ok and time_ok and not improved and not fixed and not regressed:
        keep = False  # flat → no keep
    decision = {
        "lever": lever,
        "keep": keep,
        "reason": {
            "score_ok": score_ok,
            "time_ok": time_ok,
            "improved": improved,
            "fixed_cases": fixed,
            "regressed_cases": regressed,
            "prev_ok_gold": pog,
            "cur_ok_gold": cog,
            "prev_wall_sec": pwall,
            "cur_wall_sec": cwall,
            "time_ratio": round(cwall / pwall, 3) if pwall else None,
            "time_cap": time_cap,
        },
    }
    if not time_ok:
        decision["verdict"] = "revert: tiempo > 1.5× baseline"
    elif regressed:
        decision["verdict"] = f"revert: regresión {regressed}"
    elif keep:
        decision["verdict"] = "keep: mejora sin coste escandaloso"
    else:
        decision["verdict"] = "revert: no mejora (o flat)"
    return decision


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--base",
        type=Path,
        default=ROOT / ".tuide/ai/l2_wave/v2_night/TOOL_EVAL",
    )
    ap.add_argument("--api", default=DEFAULT_API)
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument(
        "--from-lever",
        default="",
        help="skip levers before this name (e.g. peek to resume)",
    )
    args = ap.parse_args()
    base: Path = args.base
    base.mkdir(parents=True, exist_ok=True)
    rm_done = base / "DONE"
    if rm_done.is_file():
        rm_done.unlink()
    (base / "STARTED.txt").write_text(
        f"TOOL_EVAL full {time.strftime('%Y-%m-%dT%H:%M:%S%z')}\n"
        f"model={args.model} thinking=off\n"
        f"levers={[x[1] for x in LEVERS]}\n",
        encoding="utf-8",
    )

    r0_sum = base / "R0_lite" / "R0_lite_SUMMARY.json"
    r0 = load_summary(r0_sum) if r0_sum.is_file() else seed_r0(base)
    print(f"R0 ok_gold={r0['ok_gold_frac']} wall={r0['wall_sec']}s", flush=True)

    floor = ["grep", "read"]
    kept: list[str] = []
    state: dict = {
        "thinking": False,
        "model": args.model,
        "floor": list(floor),
        "kept": kept,
        "r0": {
            "ok_gold_frac": r0["ok_gold_frac"],
            "ok_frac": r0["ok_frac"],
            "wall_sec": r0["wall_sec"],
        },
        "rounds": [],
    }
    # baseline for decide = last kept summary or r0
    prev = r0
    skip_until = args.from_lever.strip()
    skipping = bool(skip_until)

    for round_dir, lever, extras in LEVERS:
        if skipping:
            if lever == skip_until:
                skipping = False
            else:
                # still honor prior decisions for floor
                dec_p = base / round_dir / "DECISION.json"
                if dec_p.is_file():
                    d = json.loads(dec_p.read_text(encoding="utf-8"))
                    if d.get("keep"):
                        for t in extras:
                            if t not in floor:
                                floor.append(t)
                                kept.append(t)
                        sum_p = base / round_dir / f"{round_dir}_SUMMARY.json"
                        if sum_p.is_file():
                            prev = load_summary(sum_p)
                    state["rounds"].append(
                        {"round": round_dir, "lever": lever, "decision": d, "skipped_run": True}
                    )
                continue

        tools = list(floor) + [t for t in extras if t not in floor]
        # If DECISION exists and SUMMARY exists, reuse (follow/entre already done)
        dec_path = base / round_dir / "DECISION.json"
        sum_path = base / round_dir / f"{round_dir}_SUMMARY.json"
        if dec_path.is_file() and sum_path.is_file():
            cur = load_summary(sum_path)
            d = json.loads(dec_path.read_text(encoding="utf-8"))
            print(f"reuse {round_dir}: {d.get('verdict')}", flush=True)
        else:
            cur = run_round(base, round_dir, tools, args.api, args.model)
            d = decide(prev, cur, lever)
            (base / round_dir / "DECISION.json").write_text(
                json.dumps(d, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
            )
            print(json.dumps(d, indent=2, ensure_ascii=False), flush=True)

        state["rounds"].append(
            {
                "round": round_dir,
                "lever": lever,
                "tools": tools,
                "ok_gold_frac": cur.get("ok_gold_frac"),
                "ok_frac": cur.get("ok_frac"),
                "wall_sec": cur.get("wall_sec"),
                "decision": d,
            }
        )
        if d.get("keep"):
            for t in extras:
                if t not in floor:
                    floor.append(t)
                    kept.append(t)
            prev = cur  # new baseline for next lever
        # if revert, prev stays (compare next lever vs last kept / r0)

        (base / "STATE.json").write_text(
            json.dumps({**state, "floor": floor, "kept": kept}, indent=2, ensure_ascii=False)
            + "\n",
            encoding="utf-8",
        )

    state["floor_final"] = floor
    state["kept"] = kept
    (base / "STATE.json").write_text(
        json.dumps(state, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (base / "DONE").write_text(time.strftime("%Y-%m-%dT%H:%M:%S%z") + "\n", encoding="utf-8")
    print("TOOL_EVAL DONE floor_final=" + ",".join(floor) + " kept=" + ",".join(kept), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
