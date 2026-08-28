#!/usr/bin/env python3
"""Core5 batteries: L1 ProblemFrame → anchor graph → F1 hunt.

Usage:
  python3 tools/l2_core5_battery.py l1 --label baseline_l1
  python3 tools/l2_core5_battery.py entityness --label entity_v1 --from-round DIR
  python3 tools/l2_core5_battery.py decompose --label decomp_v1
  python3 tools/l2_core5_battery.py l1-entity --label core5_l1_entity_v1 --from-round DIR
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
    extract_json_blob,
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


def run_entityness_case(
    case: dict, case_dir: Path, src_dir: Path, env: dict[str, str], aliases: Path | None
) -> dict:
    """Score entityness on ProblemFrame chain links (post-L1), not raw prompt tokens."""
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    prompt = case["prompt"]
    copy_if_exists(src_dir / "map_last.md", case_dir / "map_last.md")
    copy_if_exists(src_dir / "problem_frame.json", case_dir / "problem_frame.json")

    pf_path = case_dir / "problem_frame.json"
    if not pf_path.exists():
        return {"id": cid, "rc": 2, "error": "missing_problem_frame"}

    map_path = case_dir / "map_last.md"
    if map_path.exists():
        registry_ingest_map(map_path, env)

    out_json = case_dir / "entityness.json"
    argv = [
        str(CLI),
        "entityness-probe",
        "--problem-frame-json",
        str(pf_path),
        "--query",
        prompt,
        "--out",
        str(out_json),
    ]
    if aliases and aliases.exists():
        argv += ["--aliases-json", str(aliases)]
    log = case_dir / "entityness_probe.log"
    rc = run_cmd(argv, log, env, timeout=180)
    ej = {}
    if out_json.exists():
        try:
            ej = json.loads(out_json.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            ej = {}
    links = ej.get("links") or []
    best = {}
    if links:
        best = max(links, key=lambda x: float(x.get("entityness") or 0))
    return {
        "id": cid,
        "rc": rc,
        "explore_mode": ej.get("explore_mode"),
        "best_role": ej.get("best_role"),
        "best_entityness": ej.get("best_entityness"),
        "top_link_terms": (best.get("search_terms") if isinstance(best, dict) else None),
        "top_owner_stem": best.get("owner_stem") if isinstance(best, dict) else None,
        "n_links": len(links),
    }


def _units_at_stop(decomp: dict) -> list[dict]:
    levels = decomp.get("levels") or []
    if not levels:
        return []
    stop = decomp.get("stop_level")
    if stop is None:
        stop = levels[-1].get("level", 0)
    for lv in levels:
        if int(lv.get("level", -1)) == int(stop):
            return [u for u in (lv.get("units") or []) if isinstance(u, dict)]
    return [u for u in (levels[-1].get("units") or []) if isinstance(u, dict)]


def pf_from_decompose_units(decomp: dict, prompt: str) -> dict:
    """Map stop-level units → ProblemFrame links (focal→primary, rest→secondary)."""
    units = _units_at_stop(decomp)
    focal = [u for u in units if str(u.get("role")) == "focal"]
    rest = [u for u in units if str(u.get("role")) != "focal"]
    if not focal and units:
        focal, rest = units[:1], units[1:]
    pri = focal[0] if focal else {"label": "", "search_terms": [], "role": "focal"}
    pf = {
        "schema": "problem_frame_v1",
        "problem_kind": "locate",
        "problem_frame": "from intent_decompose_v0",
        "instruction": prompt,
        "primary_anchor": {
            "kind": "control",
            "objective": pri.get("label") or "focal",
            "search_terms": list(pri.get("search_terms") or []),
        },
        "secondary_anchors": [
            {
                "kind": str(u.get("role") or "module"),
                "objective": u.get("label") or u.get("id") or "unit",
                "search_terms": list(u.get("search_terms") or []),
                "deferred": True,
                "why_later": f"decompose {u.get('id')} role={u.get('role')}",
            }
            for u in (rest + focal[1:])
        ],
        "mechanism_gaps": [],
        "reject_noise": [],
        "anchor_confidence": "medium",
        "provenance": "intent_decompose_v0",
    }
    return pf


def run_decompose_case(case: dict, case_dir: Path, env: dict[str, str], timeout: int) -> dict:
    """L1 --intent-decompose then entityness on stop-level units as chain links."""
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    prompt = case["prompt"]
    log = case_dir / "decompose_gen.txt"
    argv = [
        str(TUIDE),
        "l1-debug",
        "--no-stem-embed",
        "--workspace",
        str(ROOT),
        "--intent-decompose",
        "--query",
        prompt,
    ]
    rc = run_cmd(argv, log, env, timeout=timeout)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    decomp = extract_json_blob(text) or {}
    if decomp:
        write_json(case_dir / "decompose.json", decomp)
    else:
        return {"id": cid, "rc": rc, "error": "no_decompose_json", "n_units": 0}

    pf = pf_from_decompose_units(decomp, prompt)
    write_json(case_dir / "problem_frame.json", pf)
    out_json = case_dir / "entityness.json"
    eargv = [
        str(CLI),
        "entityness-probe",
        "--problem-frame-json",
        str(case_dir / "problem_frame.json"),
        "--query",
        prompt,
        "--out",
        str(out_json),
    ]
    erc = run_cmd(eargv, case_dir / "entityness_probe.log", env, timeout=180)
    ej = {}
    if out_json.exists():
        try:
            ej = json.loads(out_json.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            ej = {}
    units = _units_at_stop(decomp)
    return {
        "id": cid,
        "rc": rc,
        "entity_rc": erc,
        "stop_level": decomp.get("stop_level"),
        "stop_reason": decomp.get("stop_reason"),
        "n_levels": len(decomp.get("levels") or []),
        "n_units": len(units),
        "roles": [u.get("role") for u in units],
        "explore_mode": ej.get("explore_mode"),
        "best_entityness": ej.get("best_entityness"),
        "best_role": ej.get("best_role"),
    }


def run_decompose_battery(label: str, cases_path: Path, only: str, timeout: int) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]
    rows = []
    for case in cases:
        print(f"=== DECOMPOSE {case['id']} ===", flush=True)
        kill_runtime()
        time.sleep(0.5)
        rows.append(run_decompose_case(case, out / case["id"], env, timeout))
    (out / "decompose_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_intent_decompose.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    return out


def run_entityness_battery(
    label: str, cases_path: Path, from_round: Path, only: str, aliases: Path | None
) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]

    # Ingest all maps first so registry is warm.
    for case in cases:
        src = from_round / case["id"]
        mp = src / "map_last.md"
        if mp.exists():
            print(f"=== ENTITY ingest {case['id']} ===", flush=True)
            registry_ingest_map(mp, env)

    rows = []
    for case in cases:
        print(f"=== ENTITYNESS {case['id']} ===", flush=True)
        src = from_round / case["id"]
        rows.append(run_entityness_case(case, out / case["id"], src, env, aliases))

    (out / "entityness_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_entityness.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    return out


def run_l1_entity_case(
    case: dict, case_dir: Path, entity_src: Path, env: dict[str, str], timeout: int
) -> dict:
    """L1 with --entityness-json from a prior entityness round."""
    case_dir.mkdir(parents=True, exist_ok=True)
    cid = case["id"]
    prompt = case["prompt"]
    ej = entity_src / "entityness.json"
    copy_if_exists(ej, case_dir / "entityness.json")
    copy_if_exists(entity_src / "map_last.md", case_dir / "map_last.md")

    kill_runtime()
    map_out = case_dir / "map_last.md"
    seeds_out = case_dir / "seeds.json"
    l1_log = case_dir / "l1_gen.txt"
    argv = [
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
    ]
    if (case_dir / "entityness.json").exists():
        argv += ["--entityness-json", str(case_dir / "entityness.json")]
    rc = run_cmd(argv, l1_log, env, timeout=timeout)

    pf, prov = extract_pf_from_l1_log(l1_log.read_text(errors="replace"))
    if not pf:
        pf = fallback_pf_from_query(prompt)
        prov = "deterministic_fallback"
    pf = refine_pf_from_query(pf, prompt)
    pf.setdefault("schema", "problem_frame_v1")
    pf["provenance"] = prov
    write_json(case_dir / "problem_frame.json", pf)
    write_json(
        case_dir / "problem_frame_meta.json",
        {"provenance": prov, "l1_rc": rc, "entityness": True},
    )
    return {"id": cid, "l1_rc": rc, "provenance": prov, "terms": pf_search_terms(pf)}


def run_l1_entity_battery(
    label: str, cases_path: Path, from_entity: Path, only: str, timeout: int
) -> Path:
    ensure_bins()
    env = env_base()
    out = BATTERY_ROOT / f"round_{label}"
    out.mkdir(parents=True, exist_ok=True)
    cases = load_cases(cases_path)
    if only:
        cases = [c for c in cases if c["id"] == only]
    rows = []
    for case in cases:
        print(f"=== L1+ENTITY {case['id']} ===", flush=True)
        src = from_entity / case["id"]
        rows.append(run_l1_entity_case(case, out / case["id"], src, env, timeout))
    (out / "l1_results.json").write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    subprocess.call(
        [
            sys.executable,
            str(ROOT / "tools/l2_explore_battery/score_problem_frame.py"),
            "--round-dir",
            str(out),
            "--cases",
            str(cases_path),
        ]
    )
    return out


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
    ap.add_argument(
        "phase",
        choices=("probe", "l1", "graph", "f1", "all", "rescore-l1", "entityness", "l1-entity", "decompose"),
    )
    ap.add_argument("--label", default="core5_v1")
    ap.add_argument("--cases", type=Path, default=CASES_DEFAULT)
    ap.add_argument("--from-round", type=Path, default=None, help="prior round dir for graph/f1/entity inputs")
    ap.add_argument("--aliases-json", type=Path, default=None, help="optional aliases map for entityness")
    ap.add_argument("--only", default="", help="single case id")
    ap.add_argument("--timeout", type=int, default=2400, help="per-case timeout seconds")
    ap.add_argument("--check-gate", action="store_true")
    args = ap.parse_args()

    if args.phase == "probe":
        ok, reason = llm_probe()
        print(json.dumps({"ok": ok, "reason": reason}, indent=2))
        return 0 if ok else 2

    if args.phase == "decompose":
        out = run_decompose_battery(args.label, args.cases, args.only, args.timeout)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_intent_decompose.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

    if args.phase == "entityness":
        fr = args.from_round
        if fr is None:
            raise SystemExit("--from-round required for entityness (L1 round with map_last.md)")
        out = run_entityness_battery(args.label, args.cases, fr, args.only, args.aliases_json)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_entityness.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

    if args.phase == "l1-entity":
        # Deprecated name: entityness is post-PF. Run link scoring on --from-round L1 outputs.
        print("note: l1-entity → entityness on ProblemFrame links (no L1 re-inject)", flush=True)
        fr = args.from_round
        if fr is None:
            raise SystemExit("--from-round required (L1 round with problem_frame.json)")
        out = run_entityness_battery(args.label, args.cases, fr, args.only, args.aliases_json)
        if args.check_gate:
            return subprocess.call(
                [
                    sys.executable,
                    str(ROOT / "tools/l2_explore_battery/score_entityness.py"),
                    "--round-dir",
                    str(out),
                    "--cases",
                    str(args.cases),
                    "--check-gate",
                ]
            )
        return 0

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
