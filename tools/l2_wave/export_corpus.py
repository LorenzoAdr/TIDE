#!/usr/bin/env python3
"""A1: exporta trazas curadas sense* → tests/fixtures/l2_wave/corpus/."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path


def _git(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except subprocess.CalledProcessError:
        return ""


def _load_flags(night: Path) -> dict:
    p = night / "flags_snapshot.json"
    if p.is_file():
        return json.loads(p.read_text(encoding="utf-8"))
    return {}


def last_control_user(case_dir: Path) -> Path | None:
    nums = []
    for p in case_dir.glob("control_*"):
        if not p.is_dir():
            continue
        name = p.name
        if not name.startswith("control_"):
            continue
        suf = name[len("control_") :]
        if suf.isdigit():
            nums.append(int(suf))
    if not nums:
        return None
    n = max(nums)
    user = case_dir / f"control_{n}" / "user.md"
    return user if user.is_file() else None


def export_case(
    sense: str,
    round_dir: Path,
    case_dir: Path,
    dest_root: Path,
    manifest_meta: dict,
) -> bool:
    if not (case_dir / "control.json").is_file():
        return False
    round_id = round_dir.name.replace("round_", "")
    dest = dest_root / f"{sense}_{round_id}_{case_dir.name}"
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(case_dir / "control.json", dest / "control.json")
    if (case_dir / "jobs.md").is_file():
        shutil.copy2(case_dir / "jobs.md", dest / "jobs.md")
    if (case_dir / "plan.json").is_file():
        shutil.copy2(case_dir / "plan.json", dest / "plan.json")
    elif (case_dir / "plan.md").is_file():
        shutil.copy2(case_dir / "plan.md", dest / "plan.md")
    user = last_control_user(case_dir)
    if user:
        shutil.copy2(user, dest / "user.md")
    # last system too (handy for A6)
    if user:
        sys_p = user.with_name("system.txt")
        if sys_p.is_file():
            shutil.copy2(sys_p, dest / "system.txt")
        raw = user.with_name("raw.txt")
        if raw.is_file():
            shutil.copy2(raw, dest / "raw.txt")
        ola = user.with_name("ola.json")
        if ola.is_file():
            shutil.copy2(ola, dest / "ola.json")
    man = {
        **manifest_meta,
        "sense": sense,
        "round": round_dir.name,
        "case": case_dir.name,
        "source": str(case_dir),
    }
    (dest / "MANIFEST.json").write_text(
        json.dumps(man, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument(
        "--src",
        type=Path,
        default=root / ".tuide/ai/l2_wave",
        help="Raíz sense*",
    )
    ap.add_argument(
        "--dest",
        type=Path,
        default=root / "tests/fixtures/l2_wave/corpus",
        help="Destino corpus",
    )
    ap.add_argument("--limit", type=int, default=0, help="Máx trazas (0=todas)")
    ap.add_argument("--senses", default="sense12,sense13,sense14,sense15,sense16")
    args = ap.parse_args()
    commit = _git(root, "rev-parse", "--short", "HEAD")
    branch = _git(root, "rev-parse", "--abbrev-ref", "HEAD")
    flags = _load_flags(root / ".tuide/ai/l2_wave/v2_night")
    meta = {
        "commit": commit,
        "branch": branch,
        "model": "qwen2.5-coder-14b-instruct-q4_k_m",
        "api_base": "http://192.168.64.1:8080/v1",
        "induce": "miss-answer",
        "flags": flags,
        "exporter": "export_corpus.py",
    }
    args.dest.mkdir(parents=True, exist_ok=True)
    n = 0
    for sense_name in args.senses.split(","):
        sense = args.src / sense_name.strip()
        if not sense.is_dir():
            continue
        for rd in sorted(sense.glob("round_*")):
            for case_dir in sorted(rd.iterdir()):
                if not case_dir.is_dir():
                    continue
                if export_case(sense_name.strip(), rd, case_dir, args.dest, meta):
                    n += 1
                    if args.limit and n >= args.limit:
                        break
            if args.limit and n >= args.limit:
                break
        if args.limit and n >= args.limit:
            break
    index = {
        "n": n,
        "dest": str(args.dest),
        "meta": {k: meta[k] for k in ("commit", "branch", "model", "induce")},
    }
    (args.dest / "INDEX.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(index, ensure_ascii=False, indent=2))
    return 0 if n >= 1 else 1


if __name__ == "__main__":
    raise SystemExit(main())
