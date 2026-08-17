#!/usr/bin/env python3
"""Autonomous prompt-pack A/B sweep (~15h budget).

Runs baseline then each playbook pack against a fixed checklist eval.
Promotes packs that clearly improve facet_recall / all_pass into
tools/l2_battery/prompt_packs/promoted/ and records CONCLUSIONS.

Env overrides (continuation / hard-only):
  L2_PROMPT_SWEEP_DIR   default .tuide/ai/l2_prompt_sweep
  L2_PROMPT_PLAYBOOK    default tools/l2_battery/prompt_packs/playbook.json
  L2_PROMPT_CASES       default tools/l2_battery/prompt_packs/cases.json
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKS = ROOT / "tools/l2_battery/prompt_packs"
CASES = Path(os.environ.get("L2_PROMPT_CASES") or (PACKS / "cases.json"))
if not CASES.is_absolute():
    CASES = ROOT / CASES
PLAYBOOK = Path(os.environ.get("L2_PROMPT_PLAYBOOK") or (PACKS / "playbook.json"))
if not PLAYBOOK.is_absolute():
    PLAYBOOK = ROOT / PLAYBOOK
PROMOTED_DIR = PACKS / "promoted"
PROMOTED_ACTIVE = PACKS / "promoted_active.json"
SWEEP = Path(os.environ.get("L2_PROMPT_SWEEP_DIR") or (ROOT / ".tuide/ai/l2_prompt_sweep"))
if not SWEEP.is_absolute():
    SWEEP = ROOT / SWEEP
STATE = SWEEP / "state.json"
LOG = SWEEP / "orchestrator.log"
CLI = ROOT / "build/l2_harness_cli"
SCORE = ROOT / "tools/l2_battery/score_facets.py"
RUNNER = ROOT / "tools/l2_battery/run_closed_loop_round.py"


def log(msg: str) -> None:
    SWEEP.mkdir(parents=True, exist_ok=True)
    line = f"[{datetime.now().isoformat(timespec='seconds')}] {msg}"
    print(line, flush=True)
    with LOG.open("a", encoding="utf-8") as f:
        f.write(line + "\n")


def heartbeat(msg: str) -> None:
    SWEEP.mkdir(parents=True, exist_ok=True)
    (SWEEP / "HEARTBEAT.txt").write_text(datetime.now().isoformat(timespec="seconds") + "\n")
    (SWEEP / "STATUS.md").write_text(
        f"# L2 prompt-pack sweep\n\nDir: `{SWEEP}`\nPlaybook: `{PLAYBOOK.name}`\nCases: `{CASES.name}`\n\n{msg}\n",
        encoding="utf-8",
    )


def atomic_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=".st_", suffix=".json")
    os.close(fd)
    Path(tmp).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def load_state() -> dict:
    if STATE.exists():
        return json.loads(STATE.read_text(encoding="utf-8"))
    pb = json.loads(PLAYBOOK.read_text(encoding="utf-8"))
    return {
        "status": "running",
        "round_index": 0,  # 0 = baseline
        "started": datetime.now().isoformat(timespec="seconds"),
        "started_mono": time.monotonic(),
        "budget_hours": float(pb.get("budget_hours") or 15),
        "metrics_best": None,
        "best_pack": pb.get("baseline_pack") or "baseline.json",
        "no_promote_streak": 0,
        "incomplete_retries": 0,
        "playbook": str(PLAYBOOK),
        "cases": str(CASES),
        "history": [],
    }


def rebuild() -> None:
    subprocess.run(
        ["cmake", "--build", "build", "--target", "l2_harness_cli", "-j", str(os.cpu_count() or 4)],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def decide(best: dict | None, cur: dict, pb: dict) -> str:
    if cur.get("incomplete"):
        return "INCOMPLETE"
    if best is None:
        return "BASELINE"
    pi = pb.get("promote_if") or {}
    rel = float(pi.get("relative_gain_pct", 8))
    min_delta = int(pi.get("min_all_pass_delta", 1))
    primary = pb.get("metric_primary", "facet_recall")
    bv = float(best.get(primary) or 0)
    cv = float(cur.get(primary) or 0)
    ba = int(best.get("all_pass") or 0)
    ca = int(cur.get("all_pass") or 0)
    if ca < 0 or ba < 0:
        return "INCOMPLETE"
    gain = 0.0 if bv <= 0 else (cv - bv) / bv * 100.0
    if ca < ba:
        return "REVERT"
    if (ca - ba) >= min_delta or gain >= rel:
        return "PROMOTE"
    return "REVERT"


def run_round(round_id: str, pack_rel: str, timeout: int) -> dict:
    out = SWEEP / f"round_{round_id}"
    # Fresh tree for retries of incomplete rounds
    if out.exists() and not (out / "FINISHED.txt").exists():
        shutil.rmtree(out, ignore_errors=True)
    elif out.exists() and (out / "metrics.json").exists():
        try:
            prev = json.loads((out / "metrics.json").read_text(encoding="utf-8"))
            if prev.get("incomplete"):
                shutil.rmtree(out, ignore_errors=True)
        except Exception:
            pass
    out.mkdir(parents=True, exist_ok=True)
    pack_path = PACKS / pack_rel
    env = os.environ.copy()
    env["L2_PROMPT_PACK"] = str(pack_path)
    log(f"round={round_id} pack={pack_path} cases={CASES.name}")
    cp = subprocess.run(
        [
            sys.executable,
            str(RUNNER),
            "--cases",
            str(CASES),
            "--out",
            str(out),
            "--cli",
            str(CLI),
            "--root",
            str(ROOT),
            "--case-timeout",
            str(timeout),
        ],
        cwd=ROOT,
        env=env,
    )
    if cp.returncode != 0:
        log(f"WARN round rc={cp.returncode}")
    subprocess.run(
        [sys.executable, str(SCORE), "--cases", str(CASES), "--round-dir", str(out)],
        cwd=ROOT,
    )
    metrics_path = out / "metrics.json"
    if not metrics_path.exists():
        return {
            "facet_recall": 0.0,
            "all_pass": -1,
            "all_pass_rate": 0.0,
            "n_cases": 0,
            "incomplete": True,
            "missing_ids": ["*"],
        }
    return json.loads(metrics_path.read_text(encoding="utf-8"))


def promote_pack(pack_rel: str, metrics: dict) -> None:
    PROMOTED_DIR.mkdir(parents=True, exist_ok=True)
    src = PACKS / pack_rel
    dst = PROMOTED_DIR / src.name
    dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
    active = {
        "pack": pack_rel,
        "promoted_at": datetime.now().isoformat(timespec="seconds"),
        "metrics": {
            "facet_recall": metrics.get("facet_recall"),
            "all_pass": metrics.get("all_pass"),
            "all_pass_rate": metrics.get("all_pass_rate"),
        },
        "sweep_dir": str(SWEEP),
        "cases": str(CASES),
    }
    PROMOTED_ACTIVE.write_text(json.dumps(active, indent=2) + "\n", encoding="utf-8")
    default = PACKS / "DEFAULT_PACK"
    default.write_text(pack_rel + "\n", encoding="utf-8")
    subprocess.run(["git", "add", str(PROMOTED_DIR), str(PROMOTED_ACTIVE), str(default)], cwd=ROOT)
    subprocess.run(
        [
            "git",
            "commit",
            "-m",
            f"prompt-sweep: promote {pack_rel} (recall={metrics.get('facet_recall'):.3f} all_pass={metrics.get('all_pass')})",
        ],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    log(f"PROMOTED {pack_rel}")


def finalize(st: dict) -> None:
    summary = {
        "finished": datetime.now().isoformat(timespec="seconds"),
        "state": st,
        "playbook": str(PLAYBOOK),
        "cases": str(CASES),
        "rounds": {},
    }
    for d in sorted(SWEEP.glob("round_*")):
        m = d / "metrics.json"
        if m.exists():
            summary["rounds"][d.name] = json.loads(m.read_text(encoding="utf-8"))
    (SWEEP / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    lines = [
        "# L2 prompt-pack sweep CONCLUSIONS",
        "",
        f"Finished: {summary['finished']}",
        f"sweep_dir: {SWEEP}",
        f"playbook: {PLAYBOOK}",
        f"cases: {CASES}",
        f"best_pack: {st.get('best_pack')}",
        f"no_promote_streak: {st.get('no_promote_streak')}",
        "",
        "History:",
    ]
    for h in st.get("history") or []:
        lines.append(
            f"- {h.get('id')}: decision={h.get('decision')} recall={h.get('facet_recall')} "
            f"all_pass={h.get('all_pass')} incomplete={h.get('incomplete')}"
        )
    lines += ["", "Promoted packs in tools/l2_battery/prompt_packs/promoted/", ""]
    (SWEEP / "CONCLUSIONS.md").write_text("\n".join(lines), encoding="utf-8")
    (SWEEP / "FINISHED.txt").write_text(summary["finished"] + "\n")
    st["status"] = "finished"
    atomic_json(STATE, st)
    heartbeat("finished")
    log("PROMPT SWEEP COMPLETE")


def main() -> int:
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    SWEEP.mkdir(parents=True, exist_ok=True)
    if cmd == "status":
        print((SWEEP / "STATUS.md").read_text() if (SWEEP / "STATUS.md").exists() else "(none)")
        if STATE.exists():
            print(json.dumps(json.loads(STATE.read_text()), indent=2)[:4000])
        if (SWEEP / "FINISHED.txt").exists():
            print("FINISHED", (SWEEP / "FINISHED.txt").read_text().strip())
        print(f"config sweep={SWEEP} playbook={PLAYBOOK} cases={CASES}")
        return 0
    if cmd not in ("start", "resume"):
        print("usage: l2_prompt_sweep.py start|resume|status", file=sys.stderr)
        return 2

    subprocess.run(
        ["pkill", "-9", "-f", "./build/l2_harness_cli run"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1)
    rebuild()
    if not CLI.exists():
        log("ERROR missing l2_harness_cli")
        return 1

    pb = json.loads(PLAYBOOK.read_text(encoding="utf-8"))
    rounds = [
        {
            "id": "baseline",
            "pack": pb.get("baseline_pack") or "baseline.json",
            "description": "baseline",
        }
    ]
    rounds.extend(pb.get("rounds") or [])
    timeout = int(pb.get("case_timeout_sec") or 2700)
    stop_streak = int(pb.get("stop_after_no_promote") or 4)
    budget_h = float(pb.get("budget_hours") or 15)
    max_incomplete = int(pb.get("max_incomplete_retries") or 2)

    st = load_state()
    if cmd == "start" and not STATE.exists():
        st["started_mono"] = time.monotonic()
        st["budget_hours"] = budget_h
        st["playbook"] = str(PLAYBOOK)
        st["cases"] = str(CASES)
    elif "started_mono" not in st:
        st["started_mono"] = time.monotonic()
        st["budget_hours"] = budget_h
    st["status"] = "running"
    atomic_json(STATE, st)
    log(
        f"prompt sweep {cmd} round_index={st.get('round_index')} budget_h={budget_h} "
        f"sweep={SWEEP} playbook={PLAYBOOK.name} cases={CASES.name}"
    )

    while True:
        st = load_state()
        idx = int(st.get("round_index") or 0)
        elapsed_h = (time.monotonic() - float(st.get("started_mono") or time.monotonic())) / 3600.0
        try:
            started = datetime.fromisoformat(st.get("started"))
            wall_h = (datetime.now() - started).total_seconds() / 3600.0
            elapsed_h = max(elapsed_h, wall_h)
        except Exception:
            pass

        if idx >= len(rounds):
            log("All prompt rounds done")
            break
        if elapsed_h >= budget_h:
            log(f"Budget exhausted: {elapsed_h:.2f}h >= {budget_h}h")
            break
        if int(st.get("no_promote_streak") or 0) >= stop_streak and idx > 0:
            log(f"Stop: {stop_streak} consecutive non-promotes")
            break

        rnd = rounds[idx]
        rid = rnd["id"]
        pack = rnd["pack"]
        heartbeat(
            f"round={rid} ({idx+1}/{len(rounds)}) pack={pack} elapsed_h={elapsed_h:.2f}/{budget_h}"
        )
        log(f"=== PROMPT ROUND {rid} pack={pack} ===")

        metrics = run_round(rid, pack, timeout)
        decision = "BASELINE" if rid == "baseline" else decide(st.get("metrics_best"), metrics, pb)
        if rid == "baseline" and metrics.get("incomplete"):
            decision = "INCOMPLETE"
        log(
            f"decision={decision} recall={metrics.get('facet_recall')} "
            f"all_pass={metrics.get('all_pass')} incomplete={metrics.get('incomplete')} "
            f"missing={metrics.get('missing_ids')}"
        )

        hist = {
            "id": rid,
            "pack": pack,
            "decision": decision,
            "facet_recall": metrics.get("facet_recall"),
            "all_pass": metrics.get("all_pass"),
            "all_pass_rate": metrics.get("all_pass_rate"),
            "incomplete": bool(metrics.get("incomplete")),
            "missing_ids": metrics.get("missing_ids"),
        }
        st.setdefault("history", []).append(hist)

        analysis = SWEEP / "ANALYSIS.md"
        with analysis.open("a", encoding="utf-8") as af:
            af.write(f"## {rid}\ndecision={decision}\npack={pack}\n")
            af.write(
                json.dumps(
                    {
                        k: metrics.get(k)
                        for k in (
                            "facet_recall",
                            "all_pass",
                            "all_pass_rate",
                            "n_cases",
                            "incomplete",
                            "missing_ids",
                        )
                    },
                    indent=2,
                )
            )
            af.write("\n\n")

        if decision == "INCOMPLETE":
            retries = int(st.get("incomplete_retries") or 0) + 1
            st["incomplete_retries"] = retries
            if retries <= max_incomplete:
                log(f"INCOMPLETE retry {retries}/{max_incomplete} (same round, no advance)")
                atomic_json(STATE, st)
                continue
            log(f"INCOMPLETE exhausted retries — skip round {rid} without promote/revert streak")
            st["incomplete_retries"] = 0
            st["round_index"] = idx + 1
            atomic_json(STATE, st)
            continue

        st["incomplete_retries"] = 0
        if rid == "baseline" or decision == "BASELINE":
            st["metrics_best"] = {
                k: metrics.get(k)
                for k in ("facet_recall", "all_pass", "all_pass_rate", "n_cases", "incomplete")
            }
            st["best_pack"] = pack
            st["no_promote_streak"] = 0
        elif decision == "PROMOTE":
            promote_pack(pack, metrics)
            st["metrics_best"] = {
                k: metrics.get(k)
                for k in ("facet_recall", "all_pass", "all_pass_rate", "n_cases", "incomplete")
            }
            st["best_pack"] = pack
            st["no_promote_streak"] = 0
        else:
            st["no_promote_streak"] = int(st.get("no_promote_streak") or 0) + 1

        st["round_index"] = idx + 1
        atomic_json(STATE, st)

    finalize(load_state())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
