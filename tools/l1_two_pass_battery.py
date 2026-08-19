#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import statistics
import subprocess
import time
from collections import Counter
from pathlib import Path


TOP_RE = re.compile(r"^\s*(\d+)\.\s+(\S+):(\d+)\s+\[")
WHY_RE = re.compile(r"stem=([A-Za-z0-9_#.-]+)")
SEEDS_RE = re.compile(r"^seeds:\s*(.*)$")


def load_cases(path: Path) -> list[dict]:
    return json.loads(path.read_text(encoding="utf-8"))


def case_filter(cases: list[dict], start_at: str) -> list[dict]:
    if not start_at:
        return cases
    out = []
    keep = False
    for case in cases:
      if case["id"] == start_at:
          keep = True
      if keep:
          out.append(case)
    return out


def normalize_stem(stem: str) -> str:
    return stem.split("#", 1)[0]


def parse_ranked_entries(stdout: str) -> list[dict]:
    lines = stdout.splitlines()
    entries: list[dict] = []
    for i, line in enumerate(lines):
        m = TOP_RE.match(line)
        if not m:
            continue
        path = m.group(2)
        stem = ""
        if i + 2 < len(lines):
            m2 = WHY_RE.search(lines[i + 2])
            if m2:
                stem = normalize_stem(m2.group(1))
        parts = Path(path).parts
        top_dir = "/".join(parts[:2]) if len(parts) >= 2 else path
        entries.append(
            {
                "rank": int(m.group(1)),
                "path": path,
                "stem": stem,
                "top_dir": top_dir,
                "infra": path.startswith("src/ai/") or path.startswith("tools/") or path.startswith("tests/"),
            }
        )
    return entries


def parse_seeds(stdout: str) -> list[str]:
    for line in stdout.splitlines():
        m = SEEDS_RE.match(line.strip())
        if m:
            return [tok for tok in m.group(1).split() if tok]
    return []


def first_expected_rank(entries: list[dict], expected: list[str]) -> int:
    want = set(expected)
    for e in entries:
        if e["stem"] in want:
            return e["rank"]
        low_path = e["path"].lower()
        for stem in want:
            if stem and stem in low_path:
                return e["rank"]
    return 0


def trap_before_expected(entries: list[dict], expected_rank: int, traps: list[str]) -> bool:
    if expected_rank <= 0:
        expected_rank = 999
    trap_set = set(traps)
    for e in entries:
        if e["rank"] >= expected_rank:
            return False
        if e["stem"] in trap_set:
            return True
        low_path = e["path"].lower()
        for trap in trap_set:
            if trap and trap in low_path:
                return True
    return False


def summarize_case(case: dict, elapsed_s: float, stdout: str) -> dict:
    entries = parse_ranked_entries(stdout)
    seeds = parse_seeds(stdout)
    top5 = entries[:5]
    top10 = entries[:10]
    infra_top5 = sum(1 for e in top5 if e["infra"])
    dirs = [e["top_dir"] for e in top10]
    dir_counts = Counter(dirs)
    expected_rank = first_expected_rank(entries, case.get("expected_stems", []))
    trap_hit = trap_before_expected(entries, expected_rank, case.get("trap_stems", []))
    dominant_dir_share = (max(dir_counts.values()) / len(top10)) if top10 else 0.0
    return {
        "id": case["id"],
        "complexity": case.get("complexity"),
        "elapsed_s": round(elapsed_s, 3),
        "seed_count": len(seeds),
        "expected_rank": expected_rank,
        "expected_top5": 0 < expected_rank <= 5,
        "trap_before_expected": trap_hit,
        "infra_top5": infra_top5,
        "infra_share_top5": infra_top5 / max(len(top5), 1),
        "unique_dirs_top10": len(dir_counts),
        "dominant_dir_share_top10": dominant_dir_share,
        "top5": top5,
    }


def run_case(root: Path, cli: Path, workspace: Path, case: dict) -> tuple[float, str]:
    cmd = [
        str(cli),
        "l1-debug",
        "--no-stem-embed",
        "--workspace",
        str(workspace),
        "--query",
        case["prompt"],
    ]
    t0 = time.monotonic()
    proc = subprocess.run(
        cmd,
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        check=False,
    )
    return time.monotonic() - t0, proc.stdout


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--workspace", type=Path, required=True)
    ap.add_argument("--cases", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--start-at", default="11_restore_session_state")
    args = ap.parse_args()

    root = args.workspace
    cli = root / "build" / "tuide"
    cases = case_filter(load_cases(args.cases), args.start_at)
    args.out.mkdir(parents=True, exist_ok=True)

    rows = []
    for case in cases:
        elapsed_s, stdout = run_case(root, cli, root, case)
        row = summarize_case(case, elapsed_s, stdout)
        rows.append(row)
        (args.out / f"{case['id']}.log").write_text(stdout, encoding="utf-8")
        print(
            f"{case['id']}: t={row['elapsed_s']:.1f}s exp={row['expected_rank'] or '-'} "
            f"trap={'Y' if row['trap_before_expected'] else '-'} "
            f"infra5={row['infra_top5']} uniq10={row['unique_dirs_top10']}"
        )

    summary = {
        "cases": len(rows),
        "avg_elapsed_s": round(statistics.mean(r["elapsed_s"] for r in rows), 3) if rows else 0.0,
        "median_elapsed_s": round(statistics.median(r["elapsed_s"] for r in rows), 3) if rows else 0.0,
        "expected_top5_rate": sum(1 for r in rows if r["expected_top5"]) / max(len(rows), 1),
        "trap_before_expected_rate": sum(1 for r in rows if r["trap_before_expected"]) / max(len(rows), 1),
        "avg_infra_share_top5": sum(r["infra_share_top5"] for r in rows) / max(len(rows), 1),
        "avg_unique_dirs_top10": sum(r["unique_dirs_top10"] for r in rows) / max(len(rows), 1),
        "avg_dominant_dir_share_top10": sum(r["dominant_dir_share_top10"] for r in rows) / max(len(rows), 1),
        "rows": rows,
    }
    (args.out / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, ensure_ascii=False))


if __name__ == "__main__":
    main()
