#!/usr/bin/env python3
"""Admin tool battery: choice + harness quality + shell + multi-step chains.

Does not touch wave-explore / sense.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def load_cases(path: Path) -> list[dict]:
    data = json.loads(path.read_text(encoding="utf-8"))
    return list(data["cases"])


def script_turns(script_path: Path) -> list[dict]:
    turns = []
    for line in script_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            turns.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return turns


def last_reply(turns: list[dict], admin: dict | None) -> str:
    if admin and admin.get("reply"):
        return str(admin["reply"])
    for t in reversed(turns):
        if t.get("do") in ("cerrar", "ask_user") and t.get("reply"):
            return str(t["reply"])
    return ""


def _job_arg_blob(job: dict) -> str:
    # Prefer summary+log; tipo is separate.
    return " ".join(
        [
            str(job.get("tipo") or ""),
            str(job.get("summary") or ""),
            str(job.get("log_tail") or "")[:800],
        ]
    ).lower()


def _turn_arg(turn: dict) -> str:
    sp = turn.get("spawn") or {}
    return str(sp.get("arg") or "").strip().lower()


def score_chain(case: dict, turns: list[dict], admin: dict | None, out: dict) -> None:
    """Validate ordered multi-step chain (dummy Cursor / scripted gold)."""
    out["ok_chain"] = True
    chain = case.get("expect_chain") or []
    if not chain:
        return

    spawn_turns = [t for t in turns if t.get("do") == "spawn"]
    jobs = list((admin or {}).get("jobs") or [])
    # Pair executed jobs with script spawn turns (arg lives on the turn).
    seq = []
    n = max(len(jobs), len(spawn_turns))
    for i in range(n):
        j = jobs[i] if i < len(jobs) else None
        t = spawn_turns[i] if i < len(spawn_turns) else None
        sp = (t or {}).get("spawn") or {}
        tipo = (j or {}).get("tipo") or sp.get("tipo")
        arg_parts = [
            str(sp.get("arg") or ""),
            str((j or {}).get("summary") or ""),
            str((j or {}).get("log_tail") or "")[:800],
        ]
        seq.append({"tipo": tipo, "arg": " ".join(arg_parts).lower(), "job": j})

    need_spawns = int(case.get("expect_spawns_min") or len([s for s in chain if s.get("tipo")]))
    if len([s for s in seq if s.get("tipo")]) < need_spawns:
        out["ok_chain"] = False
        out["notes"].append(f"chain spawns={len(seq)} < {need_spawns}")

    cursor = 0
    for step_i, step in enumerate(chain):
        if step.get("do") in ("cerrar", "ask_user"):
            continue
        want_tipo = step.get("tipo")
        substrs = [s.lower() for s in (step.get("arg_any_substr") or [])]
        matched = False
        while cursor < len(seq):
            item = seq[cursor]
            cursor += 1
            tipo_ok = (want_tipo is None) or (item["tipo"] == want_tipo)
            arg_ok = True
            if substrs:
                arg_ok = any(s in item["arg"] for s in substrs)
            if tipo_ok and arg_ok:
                matched = True
                break
        if not matched:
            out["ok_chain"] = False
            out["notes"].append(
                f"chain step#{step_i + 1} miss tipo={want_tipo} substr={substrs}"
            )

    # Closing turn required when chain ends with cerrar/ask_user
    if chain and chain[-1].get("do") in ("cerrar", "ask_user"):
        last = turns[-1] if turns else {}
        if last.get("do") != chain[-1]["do"]:
            out["ok_chain"] = False
            out["notes"].append(f"chain fin do={last.get('do')} gold={chain[-1].get('do')}")
        if admin is not None and not admin.get("done"):
            out["ok_chain"] = False
            out["notes"].append("chain: admin.done=false")

    # Reply must ground in evidence from some job log/summary (not just invent)
    if case.get("chain_ground_reply") and admin:
        reply = last_reply(turns, admin).lower()
        blob = " ".join(_job_arg_blob(j) for j in jobs)
        needles = [n.lower() for n in (case.get("reply_should_mention_any") or [])]
        grounded = False
        if needles and reply:
            grounded = any(n in reply or n in blob for n in needles)
        # Also: share a token ≥4 chars between reply and last job log
        if not grounded and jobs and reply:
            log = (jobs[-1].get("log_tail") or jobs[-1].get("summary") or "").lower()
            toks = [t for t in reply.replace(",", " ").replace(".", " ").split() if len(t) >= 4]
            grounded = any(t in log for t in toks[:12])
        if not grounded:
            out["ok_reply_quality"] = False
            out["notes"].append("chain reply no anclado a inbox/jobs")

    out["harness"]["chain_spawns"] = len(seq)
    out["harness"]["chain_jobs"] = [j.get("tipo") for j in jobs]


def score_case(case: dict, turns: list[dict], admin: dict | None) -> dict:
    out = {
        "id": case["id"],
        "tool": case["tool"],
        "ok_do": False,
        "ok_tipo": False,
        "ok_arg": False,
        "ok_close": False,
        "ok_inbox": True,
        "ok_log": True,
        "ok_trunc_meta": True,
        "ok_reply_quality": True,
        "ok_notebook": True,
        "ok_chain": True,
        "illegal": False,
        "notes": [],
        "harness": {},
    }
    if not turns:
        out["notes"].append("script vacío")
        out["ok_inbox"] = False
        return out

    turn0 = turns[0]
    do = turn0.get("do")
    gold_do = case.get("gold_do")
    out["ok_do"] = do == gold_do
    if not out["ok_do"]:
        out["notes"].append(f"do={do} gold={gold_do}")

    if gold_do == "ask_user":
        out["ok_tipo"] = True
        out["ok_arg"] = True
        out["ok_close"] = do == "ask_user" and bool(turn0.get("reply"))
        reply = last_reply(turns, admin)
        if case.get("reply_must_not_be_empty") and len(reply.strip()) < 4:
            out["ok_reply_quality"] = False
            out["notes"].append("reply vacío/corto")
        return out

    spawn = turn0.get("spawn") or {}
    tipo = spawn.get("tipo")
    arg = (spawn.get("arg") or "").strip()
    arg_l = arg.lower()

    j0 = None
    if admin and admin.get("jobs"):
        j0 = admin["jobs"][0]
        tipo = j0.get("tipo") or tipo
        out["harness"] = {
            "summary": j0.get("summary", ""),
            "log_len": len(j0.get("log_tail") or ""),
            "truncated": bool(j0.get("truncated")),
            "raw_bytes": int(j0.get("raw_bytes") or 0),
            "ok": bool(j0.get("ok")),
        }

    gold_tipo = case.get("gold_tipo")
    alt = case.get("alt_tipo") or []
    if gold_tipo is None and case.get("expect_chain"):
        # Chain cases: first-step gold optional; ok if first job matches chain[0]
        step0 = (case.get("expect_chain") or [{}])[0]
        gold_tipo = step0.get("tipo") or gold_tipo
        if not alt and step0.get("alt_tipo"):
            alt = step0.get("alt_tipo") or []
    out["ok_tipo"] = (gold_tipo is None) or (tipo == gold_tipo or tipo in alt)
    if not out["ok_tipo"]:
        out["notes"].append(f"tipo={tipo} gold={gold_tipo}")

    gold_args = [a.lower() for a in (case.get("gold_arg_any") or [])]
    substrs = [a.lower() for a in (case.get("gold_arg_substr") or [])]
    has_any = case.get("gold_arg_has_any") or []

    if gold_args:
        narg = arg_l
        if narg.startswith("git "):
            narg = narg[4:].strip()
        if "cmake" in narg or "--build" in narg:
            narg = "compile"
        if narg == "build":
            narg = "compile"
        out["ok_arg"] = any(narg == g or narg.startswith(g + " ") for g in gold_args)
        if not out["ok_arg"]:
            out["notes"].append(f"arg={arg!r} gold_any={gold_args}")
    elif substrs:
        out["ok_arg"] = any(s in arg_l for s in substrs)
        if has_any:
            out["ok_arg"] = out["ok_arg"] and any(h.lower() in arg_l for h in has_any)
        if not out["ok_arg"]:
            out["notes"].append(f"arg={arg!r} need substr={substrs} has_any={has_any}")
    else:
        # Chain: first step may define arg_any_substr
        step0 = (case.get("expect_chain") or [{}])[0]
        step_subs = [s.lower() for s in (step0.get("arg_any_substr") or [])]
        if step_subs:
            out["ok_arg"] = any(s in arg_l for s in step_subs)
            if not out["ok_arg"]:
                out["notes"].append(f"arg={arg!r} chain0 substr={step_subs}")
        else:
            out["ok_arg"] = True

    # --- Harness quality ---
    if case.get("expect_inbox"):
        if not j0:
            out["ok_inbox"] = False
            out["notes"].append("sin job en admin.json")
        else:
            summary = (j0.get("summary") or "").strip()
            if len(summary) < 3:
                out["ok_inbox"] = False
                out["notes"].append("summary vacío")
            if case.get("expect_log_nonempty"):
                log = j0.get("log_tail") or ""
                if len(log.strip()) < 8:
                    out["ok_log"] = False
                    out["notes"].append("log_tail vacío (shell sin eco útil)")
            if case.get("expect_truncated_or_large"):
                trunc = bool(j0.get("truncated"))
                raw = int(j0.get("raw_bytes") or 0)
                log = j0.get("log_tail") or ""
                marked = "[truncated" in log or "--- head ---" in log or "truncated=1" in summary
                if not (trunc or raw > 2000 or marked):
                    out["ok_trunc_meta"] = False
                    out["notes"].append(
                        f"salida larga sin marca truncada (raw={raw} trunc={trunc})"
                    )
                elif trunc and not marked:
                    out["ok_trunc_meta"] = False
                    out["notes"].append("truncated=true pero inbox sin marcadores head/tail")

    reply = last_reply(turns, admin)
    if case.get("reply_must_not_be_empty") and len(reply.strip()) < 4:
        out["ok_reply_quality"] = False
        out["notes"].append("reply vacío")
    needles = [n.lower() for n in (case.get("reply_should_mention_any") or [])]
    if needles and reply:
        rl = reply.lower()
        if j0:
            log = (j0.get("log_tail") or "").lower()
            summary = (j0.get("summary") or "").lower()
            blob = rl + " " + summary + " " + log[:500]
        else:
            blob = rl
        # For chains, also scan all jobs
        if admin and admin.get("jobs"):
            for j in admin["jobs"]:
                blob += " " + _job_arg_blob(j)
        if not any(n in blob for n in needles):
            if case.get("tool") in ("shell_free", "chain"):
                out["ok_reply_quality"] = False
                out["notes"].append(f"reply no ancla en inbox/needles={needles}: {reply[:80]!r}")

    if admin:
        out["ok_close"] = bool(admin.get("done"))
        if out["ok_tipo"] and admin.get("clarify") and gold_do != "ask_user":
            out["notes"].append("cerró con ask_user tras spawn")
            out["ok_close"] = False
        nb = admin.get("notebook") or []
        need_nb = int(case.get("expect_notebook_min") or 0)
        if need_nb > 0:
            if len(nb) < need_nb:
                out["ok_notebook"] = False
                out["notes"].append(f"notebook size={len(nb)} < {need_nb}")
            else:
                out["harness"]["notebook_n"] = len(nb)
        path_needles = [p.lower() for p in (case.get("expect_notebook_path_any") or [])]
        if path_needles:
            blob_paths = []
            for e in nb:
                for p in e.get("paths") or []:
                    blob_paths.append(str(p).lower())
            if not any(any(n in p for p in blob_paths) for n in path_needles):
                out["ok_notebook"] = False
                out["notes"].append(f"notebook sin path any={path_needles} got={blob_paths[:8]}")
            else:
                out["harness"]["notebook_paths"] = blob_paths[:12]
        fact_needles = [f.lower() for f in (case.get("expect_notebook_fact_any") or [])]
        if fact_needles:
            blob_facts = []
            for e in nb:
                for f in e.get("facts") or []:
                    blob_facts.append(str(f).lower())
            if not any(any(n in f for f in blob_facts) for n in fact_needles):
                out["ok_notebook"] = False
                out["notes"].append(f"notebook sin fact any={fact_needles}")
        if case.get("expect_close") and not out["ok_close"]:
            out["notes"].append("esperaba done=true")

    if case.get("expect_chain"):
        score_chain(case, turns, admin, out)

    raw = json.dumps(turn0)
    for bad in ('"peek"', "get_code_of"):
        if bad in raw:
            out["illegal"] = True
            out["notes"].append(f"contaminación:{bad}")
    return out


def pass_case(s: dict) -> bool:
    if s["illegal"]:
        return False
    return (
        s["ok_do"]
        and s["ok_tipo"]
        and s["ok_arg"]
        and s["ok_inbox"]
        and s["ok_log"]
        and s["ok_trunc_meta"]
        and s["ok_reply_quality"]
        and s.get("ok_notebook", True)
        and s.get("ok_chain", True)
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--round", type=int, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--cases", type=Path, default=None)
    ap.add_argument("--cli", type=Path, default=None)
    ap.add_argument("--scripts-dir", type=Path, default=None)
    args = ap.parse_args()
    root = repo_root()
    cases_path = args.cases or (root / "tests/fixtures/l2_admin/battery/cases.json")
    scripts = args.scripts_dir or (
        root / "tests/fixtures/l2_admin/battery" / f"round_{args.round:02d}"
    )
    cli = args.cli or (root / "build/l2_harness_cli")
    if not cli.exists():
        print(f"missing {cli}", file=sys.stderr)
        return 2

    cases = load_cases(cases_path)
    args.out.mkdir(parents=True, exist_ok=True)
    scores = []
    for case in cases:
        cid = case["id"]
        script = scripts / f"{cid}.txt"
        case_out = args.out / cid
        case_out.mkdir(parents=True, exist_ok=True)
        run_env = os.environ.copy()
        # Web: stub determinista en battery (live = quitar TUIDE_WEB_SEARCH_STUB).
        if case.get("tool") == "web" or str(cid).startswith("chain_web"):
            run_env["TUIDE_WEB_SEARCH_STUB"] = "1"
            run_env["TUIDE_WEB_FETCH_STUB"] = "1"
        if not script.exists():
            scores.append(
                {
                    "id": cid,
                    "tool": case["tool"],
                    "pass": False,
                    "notes": [f"missing script {script}"],
                    "ok_do": False,
                    "ok_tipo": False,
                    "ok_arg": False,
                    "ok_inbox": False,
                    "ok_log": False,
                    "ok_trunc_meta": False,
                    "ok_reply_quality": False,
                    "ok_notebook": False,
                    "ok_chain": False,
                    "ok_close": False,
                    "illegal": False,
                }
            )
            continue
        cmd = [
            str(cli),
            "admin-run",
            "--out",
            str(case_out),
            "--prompt",
            case["prompt"],
            "--script",
            str(script),
        ]
        proc = subprocess.run(
            cmd, cwd=str(root), capture_output=True, text=True, env=run_env
        )
        (case_out / "stdout.txt").write_text(proc.stdout, encoding="utf-8")
        (case_out / "stderr.txt").write_text(proc.stderr, encoding="utf-8")
        admin = None
        admin_path = case_out / "admin.json"
        if admin_path.exists():
            try:
                admin = json.loads(admin_path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                admin = None
        turns = script_turns(script)
        s = score_case(case, turns, admin)
        s["pass"] = pass_case(s)
        s["exit"] = proc.returncode
        scores.append(s)

    by_tool: dict[str, list] = {}
    for s in scores:
        by_tool.setdefault(s["tool"], []).append(s)

    def tool_stats(rows: list) -> dict:
        return {
            "n": len(rows),
            "passed": sum(1 for r in rows if r.get("pass")),
            "inbox_fail": sum(1 for r in rows if not r.get("ok_inbox", True)),
            "reply_fail": sum(1 for r in rows if not r.get("ok_reply_quality", True)),
            "trunc_fail": sum(1 for r in rows if not r.get("ok_trunc_meta", True)),
            "chain_fail": sum(1 for r in rows if not r.get("ok_chain", True)),
            "cases": rows,
        }

    summary = {
        "round": args.round,
        "n": len(scores),
        "passed": sum(1 for s in scores if s.get("pass")),
        "by_tool": {t: tool_stats(rows) for t, rows in by_tool.items()},
        "cases": scores,
    }
    (args.out / "score.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
