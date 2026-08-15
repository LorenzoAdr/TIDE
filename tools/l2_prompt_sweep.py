#!/usr/bin/env python3
"""Autonomous prompt-pack A/B sweep (~15h budget).

Runs baseline then each playbook pack against a fixed checklist eval.
Promotes packs that clearly improve facet_recall / all_pass into
tools/l2_battery/prompt_packs/promoted/ and records CONCLUSIONS.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKS = ROOT / "tools/l2_battery/prompt_packs"
CASES = PACKS / "cases.json"
PLAYBOOK = PACKS / "playbook.json"
PROMOTED_DIR = PACKS / "promoted"
PROMOTED_ACTIVE = PACKS / "promoted_active.json"
SWEEP = ROOT / ".tuide/ai/l2_prompt_sweep"
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
    (SWEEP / "STATUS.md").write_text(f"# L2 prompt-pack sweep\n\n{msg}\n", encoding="utf-8")


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
        "best_pack": "baseline.json",
        "no_promote_streak": 0,
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
    gain = 0.0 if bv <= 0 else (cv - bv) / bv * 100.0
    # also require not regressing all_pass
    if ca < ba:
        return "REVERT"
    if (ca - ba) >= min_delta or gain >= rel:
        return "PROMOTE"
    return "REVERT"


def run_round(round_id: str, pack_rel: str, timeout: int) -> dict:
    out = SWEEP / f"round_{round_id}"
    out.mkdir(parents=True, exist_ok=True)
    pack_path = PACKS / pack_rel
    env = os.environ.copy()
    env["L2_PROMPT_PACK"] = str(pack_path)
    # Also stack promoted_active extras if present (cumulative winners)
    if PROMOTED_ACTIVE.exists() and round_id != "baseline":
        env["L2_PROMPT_PACK_STACK"] = str(PROMOTED_ACTIVE)  # reserved; single pack for now
    log(f"round={round_id} pack={pack_path}")
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
        return {"facet_recall": 0.0, "all_pass": 0, "all_pass_rate": 0.0, "n_cases": 0}
    return json.loads(metrics_path.read_text(encoding="utf-8"))


def promote_pack(pack_rel: str, metrics: dict) -> None:
    PROMOTED_DIR.mkdir(parents=True, exist_ok=True)
    src = PACKS / pack_rel
    dst = PROMOTED_DIR / src.name
    dst.write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
    # Active pointer: last promoted pack (orchestrator uses as new baseline reference)
    active = {
        "pack": pack_rel,
        "promoted_at": datetime.now().isoformat(timespec="seconds"),
        "metrics": {
            "facet_recall": metrics.get("facet_recall"),
            "all_pass": metrics.get("all_pass"),
            "all_pass_rate": metrics.get("all_pass_rate"),
        },
    }
    PROMOTED_ACTIVE.write_text(json.dumps(active, indent=2) + "\n", encoding="utf-8")
    # Prefer this pack as default for future harness runs
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
        f"best_pack: {st.get('best_pack')}",
        f"no_promote_streak: {st.get('no_promote_streak')}",
        "",
        "History:",
    ]
    for h in st.get("history") or []:
        lines.append(
            f"- {h.get('id')}: decision={h.get('decision')} recall={h.get('facet_recall')} all_pass={h.get('all_pass')}"
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
            print(json.dumps(json.loads(STATE.read_text()), indent=2)[:3000])
        if (SWEEP / "FINISHED.txt").exists():
            print("FINISHED", (SWEEP / "FINISHED.txt").read_text().strip())
        return 0
    if cmd not in ("start", "resume"):
        print("usage: l2_prompt_sweep.py start|resume|status", file=sys.stderr)
        return 2

    subprocess.run(["pkill", "-9", "-f", "./build/l2_harness_cli run"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    rebuild()
    if not CLI.exists():
        log("ERROR missing l2_harness_cli")
        return 1

    pb = json.loads(PLAYBOOK.read_text(encoding="utf-8"))
    rounds = [{"id": "baseline", "pack": pb.get("baseline_pack") or "baseline.json", "description": "baseline"}]
    rounds.extend(pb.get("rounds") or [])
    timeout = int(pb.get("case_timeout_sec") or 2700)
    stop_streak = int(pb.get("stop_after_no_promote") or 4)
    budget_h = float(pb.get("budget_hours") or 15)

    st = load_state()
    if cmd == "start" and not STATE.exists():
        st["started_mono"] = time.monotonic()
        st["budget_hours"] = budget_h
    elif "started_mono" not in st:
        # resume without mono: approximate remaining from wall clock started
        st["started_mono"] = time.monotonic()
        st["budget_hours"] = budget_h
    st["status"] = "running"
    atomic_json(STATE, st)
    log(f"prompt sweep {cmd} round_index={st.get('round_index')} budget_h={budget_h}")

    while True:
        st = load_state()
        idx = int(st.get("round_index") or 0)
        elapsed_h = (time.monotonic() - float(st.get("started_mono") or time.monotonic())) / 3600.0
        # If resumed, started_mono resets — also check wall from started ISO
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
        log(
            f"decision={decision} recall={metrics.get('facet_recall')} all_pass={metrics.get('all_pass')}"
        )

        hist = {
            "id": rid,
            "pack": pack,
            "decision": decision,
            "facet_recall": metrics.get("facet_recall"),
            "all_pass": metrics.get("all_pass"),
            "all_pass_rate": metrics.get("all_pass_rate"),
        }
        st.setdefault("history", []).append(hist)

        analysis = SWEEP / "ANALYSIS.md"
        with analysis.open("a", encoding="utf-8") as af:
            af.write(f"## {rid}\ndecision={decision}\npack={pack}\n")
            af.write(
                json.dumps(
                    {
                        k: metrics.get(k)
                        for k in ("facet_recall", "all_pass", "all_pass_rate", "n_cases")
                    },
                    indent=2,
                )
            )
            af.write("\n\n")

        if rid == "baseline" or decision == "BASELINE":
            st["metrics_best"] = metrics
            st["best_pack"] = pack
            st["no_promote_streak"] = 0
        elif decision == "PROMOTE":
            promote_pack(pack, metrics)
            st["metrics_best"] = metrics
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
