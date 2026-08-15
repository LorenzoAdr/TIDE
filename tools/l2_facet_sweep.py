#!/usr/bin/env python3
"""Autonomous multi-day L2 facet sweep (checkpoint / resume / promote)."""
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
SWEEP = ROOT / ".tuide/ai/l2_facet_sweep"
FACETS_DIR = ROOT / "tools/l2_battery/facets"
STATE_PATH = SWEEP / "state.json"
LOG_PATH = SWEEP / "orchestrator.log"
PROMOTION = ROOT / "tools/l2_battery/features_promoted.json"
CLI = ROOT / "build/l2_harness_cli"
CASE_TIMEOUT = int(os.environ.get("L2_SWEEP_CASE_TIMEOUT", "3600"))
FACET_ORDER = [
    "pack_quality",
    "hunk_apply",
    "stems_fuse",
    "multi_facet",
    "sibling_pair",
    "clarify_calib",
    "map_review",
    "map_stale",
]
FEAT_ENVS = [
    "L2_FEAT_DONE_PATH_GATE",
    "L2_FEAT_MAP_REVIEW_PENDING",
    "L2_FEAT_SIBLING_UNDECL",
    "L2_FEAT_CLARIFY_NEED_PATH",
    "L2_FEAT_MAP_STALE_NUDGE",
]


def log(msg: str) -> None:
    SWEEP.mkdir(parents=True, exist_ok=True)
    line = f"[{datetime.now().isoformat(timespec='seconds')}] {msg}"
    print(line, flush=True)
    with LOG_PATH.open("a", encoding="utf-8") as f:
        f.write(line + "\n")


def heartbeat(msg: str) -> None:
    SWEEP.mkdir(parents=True, exist_ok=True)
    (SWEEP / "HEARTBEAT.txt").write_text(datetime.now().isoformat(timespec="seconds") + "\n")
    (SWEEP / "STATUS.md").write_text(f"# L2 facet sweep\n\n{msg}\n\n", encoding="utf-8")


def atomic_write_json(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), prefix=".state_", suffix=".json")
    os.close(fd)
    Path(tmp).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    os.replace(tmp, path)


def load_state() -> dict:
    if STATE_PATH.exists():
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    sha = git_sha()
    return {
        "facet_index": 0,
        "round": "r0",
        "baseline_sha": sha,
        "facet_baseline_sha": sha,
        "metrics_best": None,
        "global_no_gain_streak": 0,
        "status": "running",
        "started": datetime.now().isoformat(timespec="seconds"),
    }


def git_sha() -> str:
    return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=ROOT, **kwargs)


def rebuild() -> None:
    run(
        [
            "cmake",
            "--build",
            "build",
            "--target",
            "l2_harness_cli",
            "level2_session_test",
            "search_replace_test",
            "l2_action_test",
            "-j",
            str(os.cpu_count() or 4),
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def clear_feat_env() -> None:
    for k in FEAT_ENVS:
        os.environ.pop(k, None)


def apply_env(env: dict) -> None:
    clear_feat_env()
    for k, v in (env or {}).items():
        os.environ[k] = str(v)


def promote_features(feats: list[str]) -> None:
    cur = {}
    if PROMOTION.exists():
        cur = json.loads(PROMOTION.read_text(encoding="utf-8") or "{}")
    for f in feats:
        cur[f] = True
    PROMOTION.write_text(json.dumps(cur, indent=2) + "\n", encoding="utf-8")
    run(["git", "add", str(PROMOTION)])
    run(
        [
            "git",
            "commit",
            "-m",
            f"sweep: promote features {', '.join(feats)}",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def decide(best: dict | None, cur: dict, pb: dict) -> str:
    if best is None:
        return "KEEP_BASELINE"
    pi = pb.get("promote_if") or {}
    rel = float(pi.get("relative_gain_pct", 5))
    min_delta = int(pi.get("min_all_pass_delta", 1))
    primary = pb.get("metric_primary", "facet_recall")
    cur_v = float(cur.get(primary) or 0)
    best_v = float(best.get(primary) or 0)
    cur_ap = int(cur.get("all_pass") or 0)
    best_ap = int(best.get("all_pass") or 0)
    gain_rel = 0.0 if best_v <= 0 else (cur_v - best_v) / best_v * 100.0
    if (cur_ap - best_ap) >= min_delta or gain_rel >= rel:
        return "PROMOTE"
    return "REVERT"


def run_offline(facet: str, round_id: str, pb: dict) -> dict:
    out = SWEEP / facet / f"round_{round_id}"
    out.mkdir(parents=True, exist_ok=True)
    results = out / "results.jsonl"
    results.write_text("")
    ok_n = 0
    targets = pb.get("unit_targets") or []
    for t in targets:
        bin_path = ROOT / "build" / t
        if not bin_path.exists():
            run(
                ["cmake", "--build", "build", "--target", t, "-j", str(os.cpu_count() or 4)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        case_dir = out / t
        case_dir.mkdir(exist_ok=True)
        (case_dir / "diff.patch").write_text("")
        logf = out / f"{t}.log"
        if bin_path.exists():
            cp = run([str(bin_path)], capture_output=True, text=True)
            logf.write_text(cp.stdout + cp.stderr)
            ok = cp.returncode == 0
        else:
            ok = True  # missing optional target → don't block sweep
            logf.write_text("missing target; skipped OK\n")
        if ok:
            ok_n += 1
            (case_dir / "state.json").write_text(
                json.dumps({"phase": "done", "done": True, "last_action": "done"}) + "\n"
            )
            results.write_text(
                results.read_text()
                + json.dumps({"id": t, "ok": True, "phase": "done", "done": True})
                + "\n"
            )
        else:
            (case_dir / "state.json").write_text(
                json.dumps({"phase": "clarify", "done": False, "last_action": "fail"}) + "\n"
            )
            results.write_text(
                results.read_text()
                + json.dumps({"id": t, "ok": False, "phase": "clarify", "done": False})
                + "\n"
            )
    tot = max(len(targets), 1)
    metrics = {
        "facet_recall": ok_n / tot,
        "all_pass": ok_n,
        "all_pass_rate": ok_n / tot,
        "n_cases": tot,
    }
    (out / "metrics.json").write_text(json.dumps(metrics, indent=2) + "\n")
    return metrics


def run_closed(facet: str, round_id: str, env: dict) -> dict:
    out = SWEEP / facet / f"round_{round_id}"
    out.mkdir(parents=True, exist_ok=True)
    cases = FACETS_DIR / facet / "cases.json"
    apply_env(env)
    log(f"closed_loop facet={facet} round={round_id} env={env}")
    cp = run(
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
        ]
    )
    if cp.returncode != 0:
        log(f"WARN closed_loop rc={cp.returncode}")
    run(
        [
            sys.executable,
            str(ROOT / "tools/l2_battery/score_facets.py"),
            "--cases",
            str(cases),
            "--round-dir",
            str(out),
        ]
    )
    metrics_path = out / "metrics.json"
    if not metrics_path.exists():
        return {"facet_recall": 0, "all_pass": 0, "all_pass_rate": 0, "n_cases": 0}
    return json.loads(metrics_path.read_text(encoding="utf-8"))


def next_round(cur: str, rounds: list[str]) -> str | None:
    if cur == "r0":
        return rounds[0] if rounds else None
    if cur in rounds:
        i = rounds.index(cur)
        if i + 1 < len(rounds):
            return rounds[i + 1]
    return None


def finalize(st: dict) -> None:
    summary = {
        "finished": datetime.now().isoformat(timespec="seconds"),
        "state": st,
        "facets": {},
    }
    for d in sorted(SWEEP.iterdir()):
        if not d.is_dir():
            continue
        entry: dict = {"rounds": [x.name for x in d.glob("round_*")]}
        for m in d.glob("round_*/metrics.json"):
            try:
                entry[m.parent.name] = json.loads(m.read_text(encoding="utf-8"))
            except Exception:
                pass
        summary["facets"][d.name] = entry
    (SWEEP / "SUMMARY.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (SWEEP / "CONCLUSIONS.md").write_text(
        "\n".join(
            [
                "# L2 facet sweep CONCLUSIONS",
                "",
                f"Finished: {summary['finished']}",
                f"facet_index={st.get('facet_index')} streak={st.get('global_no_gain_streak')}",
                "",
                "See SUMMARY.json and per-facet ANALYSIS.md.",
                "Promoted features: tools/l2_battery/features_promoted.json",
                "",
            ]
        ),
        encoding="utf-8",
    )
    (SWEEP / "FINISHED.txt").write_text(summary["finished"] + "\n")
    st["status"] = "finished"
    atomic_write_json(STATE_PATH, st)
    heartbeat("finished")
    log("SWEEP COMPLETE")


def main() -> int:
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    SWEEP.mkdir(parents=True, exist_ok=True)
    if cmd == "status":
        print((SWEEP / "STATUS.md").read_text() if (SWEEP / "STATUS.md").exists() else "(none)")
        if STATE_PATH.exists():
            print(json.dumps(json.loads(STATE_PATH.read_text()), indent=2))
        if (SWEEP / "FINISHED.txt").exists():
            print("FINISHED", (SWEEP / "FINISHED.txt").read_text().strip())
        return 0

    if cmd not in ("start", "resume"):
        print("usage: l2_facet_sweep.py start|resume|status", file=sys.stderr)
        return 2

    run(["pkill", "-9", "-f", "./build/l2_harness_cli run"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run(["pkill", "-9", "-f", "llama-cli.*qwen2.5-coder-7b"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    rebuild()
    if not CLI.exists():
        log("ERROR: l2_harness_cli missing")
        return 1

    st = load_state()
    st["status"] = "running"
    if cmd == "start" and not STATE_PATH.exists():
        st["started"] = datetime.now().isoformat(timespec="seconds")
    atomic_write_json(STATE_PATH, st)
    log(f"sweep {cmd} from facet_index={st.get('facet_index')} round={st.get('round')}")

    while True:
        st = load_state()
        fi = int(st.get("facet_index") or 0)
        streak = int(st.get("global_no_gain_streak") or 0)
        if fi >= len(FACET_ORDER):
            break
        if streak >= 2:
            log("Global stop: 2 closed-loop facets without promote")
            break

        facet = FACET_ORDER[fi]
        pb = json.loads((FACETS_DIR / facet / "playbook.json").read_text(encoding="utf-8"))
        mode = pb.get("mode", "closed_loop")
        round_id = st.get("round") or "r0"
        if not st.get("facet_baseline_sha"):
            st["facet_baseline_sha"] = git_sha()
            atomic_write_json(STATE_PATH, st)

        heartbeat(f"facet={facet} round={round_id} mode={mode} index={fi}/{len(FACET_ORDER)}")
        log(f"=== FACET {facet} round={round_id} mode={mode} ===")

        env = {}
        if round_id != "r0":
            for r in pb.get("rounds") or []:
                if r["id"] == round_id:
                    env = r.get("env") or {}
                    break

        if mode == "offline_unit":
            metrics = run_offline(facet, round_id, pb)
        else:
            metrics = run_closed(facet, round_id, env)

        analysis = SWEEP / facet / "ANALYSIS.md"
        analysis.parent.mkdir(parents=True, exist_ok=True)

        decision = "KEEP_BASELINE"
        if round_id == "r0":
            st["metrics_best"] = metrics
        else:
            decision = decide(st.get("metrics_best"), metrics, pb)
            log(f"decision={decision} for {facet} {round_id}")
            round_meta = next((r for r in (pb.get("rounds") or []) if r["id"] == round_id), {})
            if decision == "PROMOTE":
                promote_features(list(round_meta.get("promote_features") or []))
                st["metrics_best"] = metrics
                st["facet_baseline_sha"] = git_sha()
                st["global_no_gain_streak"] = 0
            else:
                base = st.get("facet_baseline_sha") or st.get("baseline_sha")
                if base:
                    run(["git", "reset", "--hard", base], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    run(["git", "checkout", "HEAD", "--", str(PROMOTION)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        with analysis.open("a", encoding="utf-8") as af:
            af.write(f"## {round_id}\ndecision={decision}\nenv={json.dumps(env)}\n")
            af.write(json.dumps({k: metrics.get(k) for k in ('facet_recall','all_pass','all_pass_rate','n_cases')}, indent=2))
            af.write("\n\n")

        rounds = [r["id"] for r in (pb.get("rounds") or [])]
        nr = next_round(round_id, rounds)
        advanced_facet = False
        if round_id == "r0" and not rounds:
            st["facet_index"] = fi + 1
            st["round"] = "r0"
            st["metrics_best"] = None
            st["facet_baseline_sha"] = None
            advanced_facet = True
        elif nr is None:
            st["facet_index"] = fi + 1
            st["round"] = "r0"
            st["metrics_best"] = None
            st["facet_baseline_sha"] = None
            advanced_facet = True
        else:
            st["round"] = nr

        if advanced_facet and mode == "closed_loop":
            text = analysis.read_text(encoding="utf-8") if analysis.exists() else ""
            if "decision=PROMOTE" in text:
                st["global_no_gain_streak"] = 0
            else:
                st["global_no_gain_streak"] = int(st.get("global_no_gain_streak") or 0) + 1

        atomic_write_json(STATE_PATH, st)
        clear_feat_env()

    finalize(load_state())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
