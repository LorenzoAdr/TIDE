#!/usr/bin/env python3
"""Kill leftover L2 harness + llama-cli without touching watchers/python/sandboxes.

pkill -f './build/l2_harness_cli run' does not match the real argv
('build/l2_harness_cli run'), so a new case starts a second llama while the
old harness respawns another after its child is killed.
"""
from __future__ import annotations

import os
import signal
import subprocess
import time


def _iter_procs() -> list[tuple[int, str]]:
    out = subprocess.run(
        ["ps", "-eo", "pid=,args="],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    rows: list[tuple[int, str]] = []
    for line in out.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        pid_s, _, args = line.partition(" ")
        try:
            rows.append((int(pid_s), args.strip()))
        except ValueError:
            continue
    return rows


def _should_kill(args: str) -> bool:
    argv = args.split()
    if not argv:
        return False
    base = os.path.basename(argv[0])
    if base == "l2_harness_cli" and len(argv) >= 2 and argv[1] == "run":
        return True
    if base == "llama-cli" and "qwen2.5-coder-7b" in args:
        return True
    if base == "llama-server" and "qwen2.5-coder-7b" in args:
        return True
    # popen wrapper: sh -c -- LD_LIBRARY_PATH=... llama-cli ... qwen2.5-coder-7b ...
    if base in ("sh", "bash") and "llama-cli" in args and "qwen2.5-coder-7b" in args:
        return True
    if base in ("sh", "bash") and "llama-server" in args and "qwen2.5-coder-7b" in args:
        return True
    return False


def kill_l2_runtime(*, wait_s: float = 1.0) -> list[int]:
    """SIGKILL matching processes. Returns pids that were signaled."""
    me = os.getpid()
    signaled: list[int] = []
    for pid, args in _iter_procs():
        if pid == me:
            continue
        if not _should_kill(args):
            continue
        try:
            os.kill(pid, signal.SIGKILL)
            signaled.append(pid)
        except ProcessLookupError:
            pass
        except PermissionError:
            pass
    if wait_s > 0:
        time.sleep(wait_s)
    return signaled


if __name__ == "__main__":
    pids = kill_l2_runtime()
    print("killed", pids if pids else "(none)")
