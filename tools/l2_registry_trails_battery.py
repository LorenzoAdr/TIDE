#!/usr/bin/env python3
"""NL battery cut at registry-query --trails (no LLM locate).

1) L1 map per case
2) registry-ingest --from-map (union into .tuide/effect/registry.sqlite)
3) registry-embed once
4) registry-query --trails per prompt
5) score expected_stems vs T*

Uso:
  ./tools/l2_registry_trails_battery_run.sh [LABEL] [START_AT] [only]
  python3 tools/l2_registry_trails_battery.py --label registry_trails_v1
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

ROOT = Path(__file__).resolve().parents[1]
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
CLI = ROOT / "build/l2_harness_cli"
TUIDE = ROOT / "build/tuide"


def load_cases() -> list[dict]:
    return json.loads(PROMPTS.read_text(encoding="utf-8"))


def select_ids(cases: list[dict], start_at: str, only_one: bool) -> list[str]:
    ids = [c["id"] for c in cases]
    if not start_at:
        return ids
    if only_one:
        return [start_at] if start_at in ids else []
    if start_at not in ids:
        return ids
    return ids[ids.index(start_at) :]


def append_jsonl(path: Path, row: dict) -> None:
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(row, ensure_ascii=False) + "\n")


def run_cmd(
    argv: list[str],
    log_path: Path | None,
    timeout: int,
    env: dict[str, str],
) -> tuple[int, str, str]:
    try:
        proc = subprocess.run(
            argv,
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as e:
        stdout = e.stdout or ""
        stderr = e.stderr or ""
        if log_path:
            log_path.write_text(
                (stderr + "\n" + stdout).strip() + "\n",
                encoding="utf-8",
                errors="replace",
            )
        return 124, stdout, stderr
    stdout = proc.stdout or ""
    stderr = proc.stderr or ""
    if log_path:
        log_path.write_text(
            (stderr + "\n" + stdout).strip() + "\n",
            encoding="utf-8",
            errors="replace",
        )
    return proc.returncode, stdout, stderr


def ensure_bins(env: dict[str, str]) -> None:
    if CLI.is_file() and TUIDE.is_file():
        return
    nproc = os.cpu_count() or 4
    rc = subprocess.call(
        ["cmake", "--build", str(ROOT / "build"), "--target", "tuide", "l2_harness_cli", f"-j{nproc}"],
        cwd=str(ROOT),
        env=env,
    )
    if rc != 0 or not CLI.is_file() or not TUIDE.is_file():
        raise SystemExit("no se pudo construir tuide / l2_harness_cli")


def write_trails_md(case_dir: Path, data: dict) -> None:
    lines = [
        f"query: {data.get('query') or ''}",
        f"subgraph nodes={data.get('subgraph_nodes')} facts={data.get('subgraph_facts')}",
        "",
        "### seeds (hop0)",
    ]
    for h in data.get("seeds") or []:
        if not isinstance(h, dict):
            continue
        lines.append(
            f"- {h.get('symbol') or h.get('id')}  stem={h.get('stem')}  "
            f"cos={h.get('cosine')}"
        )
    lines += ["", "### trails"]
    for t in data.get("trails") or []:
        lines.append(f"**{t.get('id')}** score={t.get('score')} — {t.get('why') or ''}")
        parts = []
        for hop in t.get("hops") or []:
            if not isinstance(hop, dict):
                continue
            kind = hop.get("kind") or ""
            if kind == "latch":
                parts.append("latch:" + str(hop.get("symbol") or hop.get("id")))
            elif kind == "ctrl":
                cond = str(hop.get("cond") or "")
                if len(cond) > 48:
                    cond = cond[:48] + "…"
                parts.append(f"[{kind} {cond}]".strip())
            else:
                parts.append(str(hop.get("symbol") or hop.get("id")))
        lines.append("  " + " → ".join(parts))
        lines.append("")
    lines += ["", "### constellations"]
    for c in data.get("constellations") or []:
        lines.append(
            f"**{c.get('id')}** score={c.get('score')} center={c.get('center_id')} "
            f"— {c.get('why') or ''}"
        )
        lines.append(
            "  primary="
            + ",".join(c.get("primary_stems") or [])
            + " peripheral="
            + ",".join(c.get("peripheral_stems") or [])
        )
    (case_dir / "trails.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="registry_trails_v1")
    ap.add_argument("--start-at", default="")
    ap.add_argument("--only", action="store_true")
    ap.add_argument("--skip-l1", action="store_true")
    ap.add_argument("--skip-ingest", action="store_true")
    ap.add_argument("--skip-embed", action="store_true")
    ap.add_argument("--skip-query", action="store_true")
    ap.add_argument("--maps-from", default="", help="round dir with per-case map_last.md")
    ap.add_argument("--top-map", type=int, default=40)
    ap.add_argument("--top-k", type=int, default=16)
    ap.add_argument("--map-top", type=int, default=15)
    ap.add_argument("--hops", type=int, default=2)
    ap.add_argument("--threads", type=int, default=5)
    args = ap.parse_args()

    env = os.environ.copy()
    env["TUIDE_ROOT"] = str(ROOT)
    env.setdefault("L2_FEAT_L2_EXPLORE_PHASE_A", "1")

    out = ROOT / ".tuide" / "ai" / "l2_explore_battery" / f"round_{args.label}"
    out.mkdir(parents=True, exist_ok=True)
    results = out / "results.jsonl"
    if not results.exists():
        results.write_text("", encoding="utf-8")

    cases = load_cases()
    ids = select_ids(cases, args.start_at, args.only)
    by_id = {c["id"]: c for c in cases}

    maps_from = Path(args.maps_from) if args.maps_from else None

    def case_map(cid: str) -> Path:
        local = out / cid / "map_last.md"
        if local.exists() and local.stat().st_size > 80:
            return local
        if maps_from is not None:
            alt = maps_from / cid / "map_last.md"
            if alt.exists() and alt.stat().st_size > 80:
                return alt
        return local

    started = out / "STARTED.txt"
    header = (
        f"==== registry trails battery ({args.label}) {datetime.now().isoformat(timespec='seconds')} ====\n"
        f"cases={len(ids)} start_at={args.start_at or 'first'} only={int(args.only)}\n"
        f"top_map={args.top_map} query --top {args.top_k} --map-top {args.map_top} "
        f"--hops {args.hops} --threads {args.threads}\n"
        f"skip l1={int(args.skip_l1)} ingest={int(args.skip_ingest)} "
        f"embed={int(args.skip_embed)} query={int(args.skip_query)}\n"
        f"maps_from={args.maps_from or '-'}\n"
    )
    started.write_text(header, encoding="utf-8")
    print(header, end="")

    ensure_bins(env)

    # --- L1 maps ---
    if not args.skip_l1:
        for cid in ids:
            case = by_id[cid]
            case_dir = out / cid
            case_dir.mkdir(parents=True, exist_ok=True)
            map_out = case_dir / "map_last.md"
            if map_out.exists() and map_out.stat().st_size > 80:
                print(f"==== L1 skip {cid} (map exists) ====")
                continue
            print(f"==== L1 {cid} {datetime.now().isoformat(timespec='seconds')} ====")
            seeds_out = case_dir / "seeds.json"
            rc, _, _ = run_cmd(
                [
                    str(TUIDE),
                    "l1-debug",
                    "--no-stem-embed",
                    "--workspace",
                    str(ROOT),
                    "--query",
                    case["prompt"],
                    "--map-out",
                    str(map_out),
                    "--seeds-out",
                    str(seeds_out),
                ],
                case_dir / "l1_gen.txt",
                timeout=300,
                env=env,
            )
            append_jsonl(
                results,
                {"id": cid, "phase": "l1", "exit": rc, "ts": datetime.now().isoformat(timespec="seconds")},
            )
            if rc != 0 or not map_out.exists():
                print(f"  WARN l1-debug rc={rc}")

    # --- ingest union ---
    if not args.skip_ingest:
        for cid in ids:
            case_dir = out / cid
            map_out = case_dir / "map_last.md"
            if not map_out.exists():
                print(f"==== ingest skip {cid} (no map) ====")
                append_jsonl(results, {"id": cid, "phase": "ingest", "exit": 1, "error": "no_map"})
                continue
            print(f"==== ingest {cid} ====")
            t0 = time.time()
            rc, _, _ = run_cmd(
                [
                    str(CLI),
                    "registry-ingest",
                    "--from-map",
                    str(map_out),
                    "--top",
                    str(args.top_map),
                    "--json",
                ],
                case_dir / "ingest.log",
                timeout=300,
                env=env,
            )
            append_jsonl(
                results,
                {
                    "id": cid,
                    "phase": "ingest",
                    "exit": rc,
                    "sec": round(time.time() - t0, 2),
                    "ts": datetime.now().isoformat(timespec="seconds"),
                },
            )
            if rc != 0:
                print(f"  WARN ingest rc={rc}")

    # --- embed once ---
    if not args.skip_embed:
        print(f"==== embed {datetime.now().isoformat(timespec='seconds')} ====")
        t0 = time.time()
        rc, stdout, stderr = run_cmd(
            [str(CLI), "registry-embed"],
            out / "embed.log",
            timeout=1800,
            env=env,
        )
        out_txt = (stdout or stderr).strip() or stderr
        if stdout.strip():
            (out / "embed.json").write_text(stdout, encoding="utf-8")
        append_jsonl(
            results,
            {
                "id": "_embed",
                "phase": "embed",
                "exit": rc,
                "sec": round(time.time() - t0, 2),
                "ts": datetime.now().isoformat(timespec="seconds"),
            },
        )
        print(out_txt[-1500:] if len(out_txt) > 1500 else out_txt)
        if rc != 0:
            print("FAIL: registry-embed")
            return 1

    # --- query trails ---
    if not args.skip_query:
        for cid in ids:
            case = by_id[cid]
            case_dir = out / cid
            case_dir.mkdir(parents=True, exist_ok=True)
            qpath = case_dir / "query_trails.json"
            print(f"==== query {cid} {datetime.now().isoformat(timespec='seconds')} ====")
            t0 = time.time()
            qcmd = [
                str(CLI),
                "registry-query",
                "--trails",
                "--top",
                str(args.top_k),
                "--hops",
                str(args.hops),
                "--threads",
                str(args.threads),
            ]
            mp = case_map(cid)
            if mp.exists() and mp.stat().st_size > 80:
                qcmd += ["--map", str(mp), "--map-top", str(args.map_top)]
            qcmd.append(case["prompt"])
            rc, stdout, stderr = run_cmd(
                qcmd,
                case_dir / "query.log",
                timeout=180,
                env=env,
            )
            data = {}
            raw = stdout.strip()
            if raw.startswith("{"):
                try:
                    data = json.loads(raw)
                except json.JSONDecodeError:
                    end = raw.rfind("}")
                    if end > 0:
                        try:
                            data = json.loads(raw[: end + 1])
                        except json.JSONDecodeError:
                            data = {}
            if not data and stderr:
                start = stderr.find("{")
                end = stderr.rfind("}")
                if start >= 0 and end > start:
                    try:
                        data = json.loads(stderr[start : end + 1])
                    except json.JSONDecodeError:
                        data = {}
            if data:
                qpath.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
                write_trails_md(case_dir, data)
            err = None
            if not data:
                err = "no_json"
            elif not data.get("trails"):
                err = "empty_trails"
            append_jsonl(
                results,
                {
                    "id": cid,
                    "phase": "query",
                    "exit": rc,
                    "error": err,
                    "n_trails": len((data or {}).get("trails") or []),
                    "n_constellations": len((data or {}).get("constellations") or []),
                    "sec": round(time.time() - t0, 2),
                    "ts": datetime.now().isoformat(timespec="seconds"),
                },
            )
            print(
                f"  rc={rc} trails={len((data or {}).get('trails') or [])} "
                f"constellations={len((data or {}).get('constellations') or [])} err={err or '-'}"
            )

    scorer = ROOT / "tools" / "l2_explore_battery" / "score_registry_trails.py"
    print("==== score ====")
    rc = subprocess.call(
        [sys.executable, str(scorer), "--cases", str(PROMPTS), "--round-dir", str(out)],
        cwd=str(ROOT),
    )
    (out / "FINISHED.txt").write_text(
        f"finished {datetime.now().isoformat(timespec='seconds')} score_rc={rc}\n",
        encoding="utf-8",
    )
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
