#!/usr/bin/env python3
"""Core5 batteries: L1 ProblemFrame → anchor graph → F1 hunt.

Usage:
  python3 tools/l2_core5_battery.py l1 --label baseline_l1
  python3 tools/l2_core5_battery.py graph --label baseline_graph --from-round DIR
  python3 tools/l2_core5_battery.py f1 --label baseline_f1 --from-round DIR
  python3 tools/l2_core5_battery.py all --label core5_v1
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
sys.path.insert(0, str(ROOT))

from tools.l2_explore_battery.pf_battery_lib import (  # noqa: E402
    DEFAULT_CASES,
    extract_pf_from_l1_log,
    load_cases,
    normalize_to_v1,
    pf_search_terms,
    refine_pf_from_query,
    write_json,
)

TUIDE = ROOT / "build/tuide"
CLI = ROOT / "build/l2_harness_cli"
CASES_DEFAULT = DEFAULT_CASES
BATTERY_ROOT = ROOT / ".tuide/ai/l2_explore_battery"


def env_base() -> dict[str, str]:
    e = os.environ.copy()
    e["TUIDE_ROOT"] = str(ROOT)
    e.setdefault("L2_FEAT_L2_EXPLORE_PHASE_A", "1")
    e.setdefault("L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY", "1")
    return e


def ensure_bins() -> None:
    if TUIDE.is_file() and CLI.is_file():
        return
    nproc = os.cpu_count() or 4
    rc = subprocess.call(
        ["cmake", "--build", str(ROOT / "build"), "--target", "tuide", "l2_harness_cli", f"-j{nproc}"],
        cwd=str(ROOT),
    )
    if rc != 0:
        raise SystemExit("build failed")


def kill_runtime() -> None:
    subprocess.run(
        ["python3", str(ROOT / "tools/l2_battery/kill_l2_runtime.py")],
        cwd=str(ROOT),
        capture_output=True,
    )


def run_cmd(argv: list[str], log_path: Path, env: dict[str, str], timeout: int = 900) -> int:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        proc = subprocess.run(
            argv,
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        log_path.write_text("TIMEOUT\n", encoding="utf-8")
        return 124
    text = (proc.stderr or "") + "\n" + (proc.stdout or "")
    log_path.write_text(text, encoding="utf-8", errors="replace")
    return proc.returncode


def copy_if_exists(src: Path, dst: Path) -> None:
    if src.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(src.read_bytes())


def run_l1_case(case: dict, case_dir: Path, env: dict[str, str], timeout: int) -> dict:
    case_dir.mkdir(parents=True, exist_ok=True)
    prompt = case["prompt"]
    cid = case["id"]
    meta = {
        "id": cid,
        "ts": datetime.now().isoformat(timespec="seconds"),
        "prompt": prompt,
    }
    (case_dir / "meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")

    kill_runtime()
    map_out = case_dir / "map_last.md"
    seeds_out = case_dir / "seeds.json"
    l1_log = case_dir / "l1_gen.txt"
    rc = run_cmd(
        [
            str(TUIDE),
            "l1-debug",
            "--no-stem-embed",
            "--workspace",
            str(ROOT),
            "--query",
            prompt,
            "--map-out",
            str(map_out),
            "--seeds-out",
            str(seeds_out),
        ],
        l1_log,
        env,
        timeout=timeout,
    )

    pf, prov = extract_pf_from_l1_log(l1_log.read_text(errors="replace"))
    if not pf:
        pf = fallback_pf_from_query(prompt)
        prov = "deterministic_fallback"

    pf = refine_pf_from_query(pf, prompt)

    pf.setdefault("schema", "problem_frame_v1")
    pf["provenance"] = prov
    write_json(case_dir / "problem_frame.json", pf)
    write_json(case_dir / "problem_frame_meta.json", {"provenance": prov, "l1_rc": rc})

    if map_out.exists():
        copy_if_exists(map_out, ROOT / ".tuide/ai/map_last.md")

    return {"id": cid, "l1_rc": rc, "provenance": prov, "terms": pf_search_terms(pf)}


def fallback_pf_from_query(prompt: str) -> dict:
    """Tokenize-only fallback — no domain stem injection."""
    import re

    low = prompt.lower()
    kind = "explain"
    if any(x in low for x in ("bug", "error", "no funciona", "atascad", "bloquead", "falla")):
        kind = "debug"
    elif any(x in low for x in ("dónde", "donde", "qué código", "que codigo", "muéstrame")):
        kind = "locate"
    elif any(x in low for x in ("añadir", "anadir", "implement", "cambia", "quiero que")):
        kind = "implement"

    terms: list[str] = []
    for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]{3,}", prompt):
        if tok not in terms:
            terms.append(tok)
        if len(terms) >= 6:
            break

    anchor_kind = {"locate": "entrypoint", "debug": "control", "implement": "feature"}.get(
        kind, "module"
    )
    return normalize_to_v1(
        {
            "schema": "problem_frame_v1",
            "problem_kind": kind,
            "problem_frame": prompt[:200],
            "primary_anchor": {
                "kind": anchor_kind,
                "objective": "localizar el módulo o pieza de código más cercana a la petición",
                "search_terms": terms,
                "edge_hints": [],
            },
            "secondary_anchors": [],
            "mechanism_gaps": [],
            "reject_noise": [],
        }
    )


def run_l1_battery(label: str, cases_path: Path, only: str, timeout: int) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]

    rows = []
    for case in cases:
        print(f"=== L1 {case['id']} ===", flush=True)
        row = run_l1_case(case, out / case["id"], env, timeout)
        rows.append(row)

    (out / "l1_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    rc = subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_problem_frame.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    if rc != 0:
        print("score_problem_frame returned", rc)
    return out


def registry_ingest_map(map_path: Path, env: dict[str, str]) -> int:
    log = map_path.parent / "registry_ingest.log"
    return run_cmd(
        [str(CLI), "registry-ingest", "--from-map", str(map_path), "--top", "40", "--json"],
        log,
        env,
        timeout=300,
    )


def registry_embed_once(out_dir: Path, env: dict[str, str]) -> int:
    log = out_dir / "registry_embed.log"
    return run_cmd([str(CLI), "registry-embed"], log, env, timeout=1800)


def registry_query_hop0(query: str, out_json: Path, env: dict[str, str]) -> int:
    log = out_json.parent / "registry_query.log"
    return run_cmd(
        [str(CLI), "registry-query", query, "--hops", "0", "--top", "12"],
        log,
        env,
        timeout=120,
    )


def parse_registry_json_from_log(log_path: Path) -> dict:
    text = log_path.read_text(errors="replace")
    # Skip server startup lines before JSON.
    start = text.find("{")
    if start < 0:
        return {}
    depth = 0
    in_str = False
    esc = False
    end = -1
    for i in range(start, len(text)):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == "\\":
                esc = True
            elif c == '"':
                in_str = False
            continue
        if c == '"':
            in_str = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                end = i
                break
    if end <= start:
        return {}
    try:
        return json.loads(text[start : end + 1])
    except json.JSONDecodeError:
        return {}


def run_graph_case(case: dict, case_dir: Path, src_dir: Path, env: dict[str, str]) -> dict:
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    copy_if_exists(src_dir / "problem_frame.json", case_dir / "problem_frame.json")
    copy_if_exists(src_dir / "map_last.md", case_dir / "map_last.md")

    pf = json.loads((case_dir / "problem_frame.json").read_text(encoding="utf-8"))
    terms = pf_search_terms(pf)
    query = " ".join(terms[:6]) if terms else case["prompt"][:120]

    qlog = case_dir / "registry_query.log"
    registry_query_hop0(query, case_dir / "registry_hop0.json", env)
    data = parse_registry_json_from_log(qlog)
    if data:
        write_json(case_dir / "registry_hop0.json", data)

    hop_n = 0
    if data:
        hop_n = len(data.get("hits") or data.get("seeds") or [])
    return {"id": cid, "query": query, "hop0_n": hop_n}


def run_graph_battery(label: str, cases_path: Path, from_round: Path, only: str) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]

    rows = []
    for case in cases:
        print(f"=== GRAPH prep {case['id']} ===", flush=True)
        src = from_round / case["id"]
        case_dir = out / case["id"]
        case_dir.mkdir(parents=True, exist_ok=True)
        copy_if_exists(src / "problem_frame.json", case_dir / "problem_frame.json")
        copy_if_exists(src / "map_last.md", case_dir / "map_last.md")
        if (case_dir / "map_last.md").exists():
            registry_ingest_map(case_dir / "map_last.md", env)

    print("=== GRAPH registry-embed ===", flush=True)
    embed_rc = registry_embed_once(out, env)
    if embed_rc != 0:
        print("WARN registry-embed rc=", embed_rc)

    for case in cases:
        print(f"=== GRAPH query {case['id']} ===", flush=True)
        src = from_round / case["id"]
        rows.append(run_graph_case(case, out / case["id"], src, env))

    (out / "graph_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_anchor_graph.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    return out


def run_f1_case(case: dict, case_dir: Path, src_dir: Path, env: dict[str, str], timeout: int) -> dict:
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    prompt = case["prompt"]
    env = env.copy()
    env["L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL"] = "1"

    map_out = case_dir / "map_last.md"
    pf_path = case_dir / "problem_frame.json"
    if (src_dir / "map_last.md").exists() and (src_dir / "problem_frame.json").exists():
        copy_if_exists(src_dir / "map_last.md", map_out)
        copy_if_exists(src_dir / "problem_frame.json", pf_path)
        copy_if_exists(src_dir / "seeds.json", case_dir / "seeds.json")
    else:
        run_l1_case(case, case_dir, env, timeout=min(timeout, 600))

    copy_if_exists(map_out, ROOT / ".tuide/ai/map_last.md")

    boot_log = case_dir / "bootstrap.txt"
    boot_argv = [str(CLI), "bootstrap"]
    if (case_dir / "seeds.json").exists():
        boot_argv += ["--seeds", str(case_dir / "seeds.json")]
    if pf_path.exists():
        boot_argv += ["--problem-frame-json", str(pf_path)]
    boot_argv.append(prompt)
    boot_rc = run_cmd(boot_argv, boot_log, env, timeout=120)
    copy_if_exists(ROOT / ".tuide/ai/l2/problem_frame.json", pf_path)

    run_log = case_dir / "run.log"
    explore_rc = run_cmd([str(CLI), "run-explore-a"], run_log, env, timeout=timeout)

    for name in ("state.json", "a_state.json", "a_notes.md", "session.md"):
        copy_if_exists(ROOT / ".tuide/ai/l2" / name, case_dir / name)
    copy_if_exists(ROOT / ".tuide/ai/l2/problem_frame.json", case_dir / "problem_frame.json")

    return {"id": cid, "bootstrap_rc": boot_rc, "explore_rc": explore_rc}


def run_f1_battery(label: str, cases_path: Path, from_round: Path, only: str, timeout: int) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]

    kill_runtime()
    rows = []
    for case in cases:
        print(f"=== F1 {case['id']} ===", flush=True)
        src = from_round / case["id"]
        rows.append(run_f1_case(case, out / case["id"], src, env, timeout))

    (out / "f1_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_f1_anchor.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    return out


def llm_probe() -> tuple[bool, str]:
    ensure_bins()
    env = env_base()
    log = BATTERY_ROOT / "llm_probe.log"
    rc = run_cmd(
        [str(TUIDE), "l1-debug", "--no-stem-embed", "--workspace", str(ROOT), "--query", "test probe"],
        log,
        env,
        timeout=180,
    )
    text = log.read_text(errors="replace")
    if "llama ensure_ready" in text or "L2 brain ensure_ready" in text:
        return False, "llm_not_ready"
    if rc == 124:
        return False, "timeout"
    if "L1 intent raw:" in text:
        return True, "l1_distill_ok"
    if "symbol index empty" in text:
        return False, "symbol_index_empty"
    return rc == 0, "rc=" + str(rc)


def rescore_l1_from_logs(round_dir: Path, cases_path: Path) -> None:
    """Re-extract problem_frame from saved l1_gen.txt (no LLM)."""
    cases = load_cases(cases_path)
    for case in cases:
        cid = case["id"]
        case_dir = round_dir / cid
        l1_log = case_dir / "l1_gen.txt"
        if not l1_log.exists():
            continue
        pf, prov = extract_pf_from_l1_log(l1_log.read_text(errors="replace"))
        if not pf:
            pf = fallback_pf_from_query(case["prompt"])
            prov = "deterministic_fallback"
        pf = refine_pf_from_query(pf, case["prompt"])
        pf.setdefault("schema", "problem_frame_v1")
        pf["provenance"] = prov
        write_json(case_dir / "problem_frame.json", pf)
        write_json(case_dir / "problem_frame_meta.json", {"provenance": prov, "rescored": True})


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("phase", choices=("probe", "l1", "graph", "f1", "all", "rescore-l1"))
    ap.add_argument("--label", default="core5_v1")
    ap.add_argument("--cases", type=Path, default=CASES_DEFAULT)
    ap.add_argument("--from-round", type=Path, default=None, help="prior round dir for graph/f1 inputs")
    ap.add_argument("--only", default="", help="single case id")
    ap.add_argument("--timeout", type=int, default=2400, help="per-case timeout seconds")
    ap.add_argument("--check-gate", action="store_true")
    args = ap.parse_args()

    if args.phase == "probe":
        ok, reason = llm_probe()
        print(json.dumps({"ok": ok, "reason": reason}, indent=2))
        return 0 if ok else 2

    if args.phase == "rescore-l1":
        out = BATTERY_ROOT / f"round_{args.label}"
        rescore_l1_from_logs(out, args.cases)
        return subprocess.call(
            [
                sys.executable,
                str(ROOT / "tools/l2_explore_battery/score_problem_frame.py"),
                "--round-dir",
                str(out),
                "--cases",
                str(args.cases),
                "--check-gate",
            ]
        )

    if args.phase == "l1":
        out = run_l1_battery(args.label, args.cases, args.only, args.timeout)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_problem_frame.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

    if args.phase == "graph":
        if not args.from_round:
            raise SystemExit("--from-round required for graph phase")
        out = run_graph_battery(args.label, args.cases, args.from_round, args.only)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_anchor_graph.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

    if args.phase == "f1":
        fr = args.from_round or (BATTERY_ROOT / f"round_{args.label.replace('f1', 'l1')}")
        if not fr.exists():
            fr = BATTERY_ROOT / "round_core5_l1"
        out = run_f1_battery(args.label, args.cases, fr, args.only, args.timeout)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_f1_anchor.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

    # all
    ok, reason = llm_probe()
    print("probe:", ok, reason)
    if not ok:
        print("LLM unavailable — infra ready; run batteries when model is up.", file=sys.stderr)
        return 2

    l1_out = run_l1_battery(args.label + "_l1", args.cases, args.only, args.timeout)
    rc1 = subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_problem_frame.py"),
            "--round-dir",
            str(l1_out),
            "--cases",
            str(args.cases),
            "--check-gate",
        ]
    )
    if rc1 != 0:
        return rc1

    g_out = run_graph_battery(args.label + "_graph", args.cases, l1_out, args.only)
    rc2 = subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_anchor_graph.py"),
            "--round-dir",
            str(g_out),
            "--cases",
            str(args.cases),
            "--check-gate",
        ]
    )
    if rc2 != 0:
        return rc2

    f_out = run_f1_battery(args.label + "_f1", args.cases, l1_out, args.only, args.timeout)
    return subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_f1_anchor.py"),
            "--round-dir",
            str(f_out),
            "--cases",
            str(args.cases),
            "--check-gate",
        ]
    )


if __name__ == "__main__":
    raise SystemExit(main())
