#!/usr/bin/env python3
"""Run one closed-loop L2 battery round with per-case timeout and map seed."""
from __future__ import annotations

import argparse
import json
import os
import re
import signal
import subprocess
import time
from datetime import datetime
from pathlib import Path

from kill_l2_runtime import kill_l2_runtime


def seed_map(root: Path, prompt: str, map_seed_query: str | None) -> None:
    map_path = root / ".tuide/ai/map_last.md"
    map_path.parent.mkdir(parents=True, exist_ok=True)
    q = map_seed_query if map_seed_query else prompt
    if map_path.exists():
        text = map_path.read_text(errors="replace")
        lines = text.splitlines()
        out = []
        replaced = False
        for ln in lines:
            if ln.startswith("query:") and not replaced:
                out.append("query: " + q)
                replaced = True
            else:
                out.append(ln)
        if not replaced:
            out.insert(0, "query: " + q)
        map_path.write_text("\n".join(out) + ("\n" if text.endswith("\n") else ""))
    else:
        map_path.write_text(f"query: {q}\n\n## Ranked entries\n\n")


def restore_product(root: Path) -> None:
    subprocess.run(
        ["git", "checkout", "--", "src/ui", "src/util"],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def kill_harness() -> None:
    # argv-exact: do not pkill -f './build/...' — that misses 'build/l2_harness_cli run'
    # and can hit sandboxes whose cmdline merely *mentions* the binary.
    kill_l2_runtime()


def run_case(root: Path, cli: Path, case: dict, case_dir: Path, timeout: int) -> dict:
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    prompt = case["prompt"]
    meta = case_dir / "meta.txt"
    meta.write_text(f"==== CASE {cid} {datetime.now().isoformat(timespec='seconds')} ====\n")
    restore_product(root)
    seed_map(root, prompt, case.get("map_seed_query"))
    kill_harness()

    boot = subprocess.run(
        [str(cli), "bootstrap", prompt],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    (case_dir / "bootstrap.txt").write_text(boot.stdout + boot.stderr, errors="replace")
    if boot.returncode != 0:
        return {"id": cid, "ok": False, "error": "bootstrap", "exit": boot.returncode}

    log_path = case_dir / "run.log"
    proc = subprocess.Popen(
        [str(cli), "run"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
        start_new_session=True,  # killpg covers llama-cli grandchild
    )
    timed_out = False
    lines: list[str] = []
    start = time.time()
    last_out = start
    assert proc.stdout is not None

    def _stop_proc(force: bool = False) -> None:
        if proc.poll() is not None:
            kill_harness()
            return
        sig = signal.SIGKILL if force else signal.SIGTERM
        try:
            os.killpg(proc.pid, sig)
        except (ProcessLookupError, PermissionError):
            pass
        try:
            proc.wait(timeout=10 if not force else 5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        kill_harness()

    try:
        while True:
            if time.time() - start > timeout:
                timed_out = True
                break
            # idle watchdog: 20 min without output
            if time.time() - last_out > 1200:
                timed_out = True
                break
            line = proc.stdout.readline()
            if line:
                lines.append(line)
                last_out = time.time()
                continue
            if proc.poll() is not None:
                break
            time.sleep(0.2)
    except Exception as e:
        lines.append(f"\nrunner_error={type(e).__name__}: {e}\n")
        timed_out = True
    finally:
        if timed_out or proc.poll() is None:
            _stop_proc(force=timed_out)
    log_path.write_text("".join(lines), errors="replace")
    rc = proc.returncode if proc.returncode is not None else -1
    if timed_out:
        lines.append(f"\nrun ok=0 phase=timeout steps=0 — case_timeout {timeout}s\n")
        log_path.write_text("".join(lines), errors="replace")

    subprocess.run(
        [str(cli), "status"],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    for name in ("state.json", "session.md", "debrief.md"):
        src = root / ".tuide/ai/l2" / name
        if src.exists():
            (case_dir / name).write_text(src.read_text(errors="replace"))
    subprocess.run(
        ["git", "diff", "--stat", "--", "src/ui", "src/util"],
        cwd=root,
        capture_output=True,
        text=True,
    )
    diff_stat = subprocess.run(
        ["git", "diff", "--stat", "--", "src/ui", "src/util"],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout
    diff_patch = subprocess.run(
        ["git", "diff", "--", "src/ui", "src/util"],
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout
    (case_dir / "diff_stat.txt").write_text(diff_stat, errors="replace")
    (case_dir / "diff.patch").write_text(diff_patch, errors="replace")

    st = {}
    if (case_dir / "state.json").exists():
        try:
            st = json.loads((case_dir / "state.json").read_text())
        except Exception:
            st = {}
    log = "".join(lines)
    actions = re.findall(r"acción=([^\s]+)", log)
    steps = [int(x) for x in re.findall(r"paso=(\d+)/", log)]
    row = {
        "id": cid,
        "exit": rc,
        "timed_out": timed_out,
        "ts": datetime.now().isoformat(timespec="seconds"),
        "phase": st.get("phase"),
        "done": st.get("done"),
        "turn": st.get("turn"),
        "last_action": st.get("last_action"),
        "max_step": max(steps) if steps else 0,
        "actions": actions,
        "n_plan": actions.count("plan"),
        "n_edit": sum(1 for a in actions if a.startswith("edit")),
        "n_tools": sum(1 for a in actions if a.startswith("tool")),
        "n_compile_ok": log.count("OK compile"),
        "n_compile_fail": log.count("compile exit_code") + log.count("FAIL compile"),
        "run_ok_line": next((ln for ln in log.splitlines() if ln.startswith("run ok=")), ""),
        "diff_stat": diff_stat.strip(),
        "product_dirty": bool(diff_stat.strip()),
    }
    restore_product(root)
    meta.write_text(
        meta.read_text()
        + f"==== DONE {cid} rc={rc} timeout={timed_out} {datetime.now().isoformat(timespec='seconds')} ====\n"
    )
    return row


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cases", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--case-timeout", type=int, default=3600)
    args = ap.parse_args()
    root = Path(args.root)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cases = json.loads(Path(args.cases).read_text(encoding="utf-8"))
    results = out / "results.jsonl"
    # Resume: skip ids already in results
    done_ids = set()
    if results.exists():
        for line in results.read_text().splitlines():
            if line.strip():
                try:
                    done_ids.add(json.loads(line)["id"])
                except Exception:
                    pass
    with results.open("a", encoding="utf-8") as rf:
        for case in cases:
            if case["id"] in done_ids:
                print(f"skip {case['id']} (already done)", flush=True)
                continue
            print(f"==== CASE {case['id']} ====", flush=True)
            try:
                row = run_case(root, Path(args.cli), case, out / case["id"], args.case_timeout)
            except Exception as e:
                kill_harness()
                restore_product(root)
                row = {
                    "id": case["id"],
                    "ok": False,
                    "error": f"{type(e).__name__}: {e}",
                    "exit": -1,
                    "timed_out": True,
                    "ts": datetime.now().isoformat(timespec="seconds"),
                    "run_ok_line": f"run ok=0 phase=runner_error — {type(e).__name__}",
                }
            rf.write(json.dumps(row, ensure_ascii=False) + "\n")
            rf.flush()
            print(row.get("run_ok_line") or row, flush=True)
    rows = [json.loads(l) for l in results.read_text().splitlines() if l.strip()]
    (out / "summary.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False) + "\n")
    (out / "FINISHED.txt").write_text(datetime.now().isoformat(timespec="seconds") + "\n")
    expected = {c["id"] for c in cases}
    got = {r.get("id") for r in rows}
    if got != expected:
        print(f"INCOMPLETE results={sorted(got)} expected={sorted(expected)}", flush=True)
        raise SystemExit(2)


if __name__ == "__main__":
    main()
