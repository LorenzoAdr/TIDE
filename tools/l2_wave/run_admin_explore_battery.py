#!/usr/bin/env python3
"""Autonomous overnight battery: probe_admin_pilot × N prompts.

Sequential. Skips cases with DONE. STOP file halts before next case.
Writes SCOREBOARD + BASELINE (axes A–D). Does not touch wave-explore / sense.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from admin_session_metrics import aggregate_baseline, score_session  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
PROBE = Path(__file__).resolve().parent / "probe_admin_pilot.py"


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def api_ok(api: str) -> bool:
    try:
        base = api.rstrip("/")
        if base.endswith("/v1"):
            health = base[: -len("/v1")] + "/health"
        else:
            health = base + "/health"
        with urllib.request.urlopen(health, timeout=5) as resp:
            return resp.status == 200
    except urllib.error.HTTPError:
        # Remote APIs (DeepSeek, etc.) don't expose /health but did answer,
        # which proves the host is up.
        return True
    except Exception:  # noqa: BLE001
        return False


def score_case(case: dict, summary: dict | None, err: str | None, case_dir: Path | None = None) -> dict:
    session = None
    if summary and isinstance(summary.get("session"), dict):
        session = summary["session"]
    elif case_dir and (case_dir / "SESSION.json").is_file():
        try:
            session = json.loads((case_dir / "SESSION.json").read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            session = None
    row = score_session(case=case, summary=summary, session=session, err=err)
    row.setdefault("kind", case.get("kind") or case.get("intent"))
    row.setdefault("difficulty", case.get("difficulty"))
    row.setdefault("style", case.get("style") or case.get("user_skill"))
    return row


def write_scoreboard(battery_out: Path, cases: list[dict], rows: list[dict]) -> None:
    baseline = aggregate_baseline(rows, n_planned=len(cases))
    by_kind: dict[str, dict] = {}
    by_diff: dict[str, dict] = {}
    for r in rows:
        for key, bucket in (("intent", by_kind), ("difficulty", by_diff)):
            k = str(r.get(key) or r.get("kind") or "?") if key == "intent" else str(r.get(key) or "?")
            b = bucket.setdefault(
                k, {"n": 0, "ok": 0, "process_ok": 0, "honest_ok": 0, "lie": 0, "flags": {}}
            )
            b["n"] += 1
            if r.get("ok"):
                b["ok"] += 1
            sc = r.get("scores") or {}
            if sc.get("process_ok"):
                b["process_ok"] += 1
            if sc.get("honest_ok"):
                b["honest_ok"] += 1
            if sc.get("lie"):
                b["lie"] += 1
            for f in r.get("flags") or []:
                b["flags"][f] = b["flags"].get(f, 0) + 1

    board = {
        "generated_at": utc_now(),
        "n_cases": len(cases),
        "n_scored": len(rows),
        "ok_frac": (sum(1 for r in rows if r.get("ok")) / len(rows)) if rows else 0.0,
        "process_ok_frac": baseline.get("process_ok_frac"),
        "honest_frac": baseline.get("honest_frac"),
        "lie_frac": baseline.get("lie_frac"),
        "terminal_mix": baseline.get("terminal_mix"),
        "by_kind": by_kind,
        "by_difficulty": by_diff,
        "by_intent": baseline.get("by_intent"),
        "by_trap": baseline.get("by_trap"),
        "rows": rows,
        "baseline": baseline,
        "weak_flags": baseline.get("weak_flags") or [],
    }
    (battery_out / "SCOREBOARD.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (battery_out / "BASELINE.json").write_text(
        json.dumps(baseline, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    lines = [
        f"# Admin vibecode battery ({board['generated_at']})",
        f"scored={board['n_scored']}/{board['n_cases']}",
        f"process_ok={baseline.get('process_ok_frac')}  honest={baseline.get('honest_frac')}  "
        f"lie={baseline.get('lie_frac')}  soft_ok={board['ok_frac']:.2f}",
        f"terminal_mix={baseline.get('terminal_mix')}",
        f"cost wall p50/p90={baseline.get('cost_wall_p50')}/{baseline.get('cost_wall_p90')}  "
        f"llm p50/p90={baseline.get('cost_llm_p50')}/{baseline.get('cost_llm_p90')}",
        "",
        "## By intent",
    ]
    for k, b in sorted((baseline.get("by_intent") or {}).items()):
        lines.append(
            f"- {k}: n={b['n']} process={b['process_ok_frac']} honest={b['honest_ok_frac']} "
            f"lie={b['lie_frac']} ask={b['ask_frac']}"
        )
    lines.extend(["", "## Weak flags"])
    for f, n in (baseline.get("weak_flags") or [])[:20]:
        lines.append(f"- {f}: {n}")
    lines.extend(["", "## Cases"])
    for r in rows:
        sc = r.get("scores") or {}
        lines.append(
            f"- {r['id']}: term={r.get('terminal_code')} exit={r.get('exit_do')} "
            f"process={sc.get('process_ok')} honest={sc.get('honest_ok')} lie={sc.get('lie')} "
            f"explores={r.get('n_explore')} shape={r.get('path_shape')} flags={r.get('flags')}"
        )
    (battery_out / "SCOREBOARD.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (battery_out / "BASELINE.md").write_text(
        "\n".join(
            [
                "# Baseline vibecode",
                f"coverage={baseline.get('coverage')}",
                f"process_ok={baseline.get('process_ok_frac')}",
                f"honest={baseline.get('honest_frac')}",
                f"lie={baseline.get('lie_frac')}",
                f"lie_deseo={baseline.get('lie_frac_deseo')}",
                f"easy_locate_ok={baseline.get('easy_locate_ok_frac')}",
                f"trap_handled={baseline.get('trap_handled_frac')}",
                f"omit_rate={baseline.get('omit_rate')}",
                f"terminal_mix={baseline.get('terminal_mix')}",
                f"path_shape={baseline.get('path_shape_hist')}",
                "",
                "Cortar: touch STOP en este directorio.",
            ]
        )
        + "\n",
        encoding="utf-8",
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--cases",
        default=str(Path(__file__).resolve().parent / "admin_vibecode_100.json"),
    )
    ap.add_argument("--out", required=True)
    ap.add_argument("--api", default="http://192.168.64.1:8080/v1")
    ap.add_argument("--model", default="qwen3.6-27b-q8_0")
    ap.add_argument("--max-turns", type=int, default=16)
    ap.add_argument("--max-explore", type=int, default=4)
    ap.add_argument("--only", default="", help="comma ids to run (debug)")
    ap.add_argument(
        "--single-agent",
        action="store_true",
        help="P16 experimental: forward --single-agent to probe_admin_pilot.py",
    )
    args = ap.parse_args()

    battery_out = Path(args.out)
    battery_out.mkdir(parents=True, exist_ok=True)
    cases = json.loads(Path(args.cases).read_text(encoding="utf-8"))["cases"]
    if args.only.strip():
        want = {x.strip() for x in args.only.split(",") if x.strip()}
        cases = [c for c in cases if c["id"] in want]

    ledger = battery_out / "ledger.jsonl"
    rows: list[dict] = []
    done_ids: set[str] = set()
    prev_board = battery_out / "SCOREBOARD.json"
    if prev_board.is_file():
        try:
            prev = json.loads(prev_board.read_text(encoding="utf-8"))
            for r in prev.get("rows") or []:
                if (battery_out / r["id"] / "DONE").is_file():
                    rows.append(r)
                    done_ids.add(r["id"])
        except Exception:  # noqa: BLE001
            pass

    (battery_out / "STARTED").write_text(utc_now() + "\n", encoding="utf-8")
    (battery_out / "README_STOP.txt").write_text(
        "Para cortar la batería sin matar el caso en curso:\n"
        f"  touch {battery_out}/STOP\n"
        "El siguiente caso no arranca. Reanuda borrando STOP (los DONE se saltan).\n",
        encoding="utf-8",
    )
    print(f"BATTERY start n={len(cases)} out={battery_out}", flush=True)

    for i, case in enumerate(cases):
        cid = case["id"]
        case_dir = battery_out / cid
        done_mark = case_dir / "DONE"
        if done_mark.is_file() or cid in done_ids:
            print(f"skip {cid} (DONE)", flush=True)
            continue

        if (battery_out / "STOP").is_file():
            print(f"STOP file present; halt before {cid}", flush=True)
            break

        for _ in range(30):
            if api_ok(args.api):
                break
            print(f"API down; sleep 60s before {cid}", flush=True)
            time.sleep(60)
        else:
            row = score_case(case, None, "api_unreachable")
            rows.append(row)
            ledger.open("a", encoding="utf-8").write(json.dumps(row, ensure_ascii=False) + "\n")
            write_scoreboard(battery_out, cases, rows)
            print(f"ABORT api unreachable at {cid}", flush=True)
            break

        case_dir.mkdir(parents=True, exist_ok=True)
        (case_dir / "case.json").write_text(
            json.dumps(case, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        print(f"==== [{i+1}/{len(cases)}] {cid} ====", flush=True)
        t0 = time.time()
        cmd = [
            sys.executable,
            "-u",
            str(PROBE),
            "--out",
            str(case_dir),
            "--api",
            args.api,
            "--model",
            args.model,
            "--prompt",
            case["prompt"],
            "--max-turns",
            str(args.max_turns),
            "--max-explore",
            str(args.max_explore),
        ]
        if args.single_agent:
            cmd.append("--single-agent")
        env = os.environ.copy()
        env["PYTHONUNBUFFERED"] = "1"
        log_path = case_dir / "battery_case.log"
        err: str | None = None
        with log_path.open("w", encoding="utf-8") as logf:
            try:
                proc = subprocess.run(
                    cmd,
                    cwd=str(ROOT),
                    env=env,
                    stdout=logf,
                    stderr=subprocess.STDOUT,
                    timeout=3 * 3600,
                    check=False,
                )
                if proc.returncode != 0:
                    err = f"exit_code={proc.returncode}"
            except subprocess.TimeoutExpired:
                err = "timeout_3h"
            except Exception as e:  # noqa: BLE001
                err = f"exception:{e}"

        summary = None
        sp = case_dir / "SUMMARY.json"
        if sp.is_file():
            try:
                summary = json.loads(sp.read_text(encoding="utf-8"))
            except Exception as e:  # noqa: BLE001
                err = err or f"summary_json:{e}"

        row = score_case(case, summary, err, case_dir=case_dir)
        row["wall_sec_measured"] = round(time.time() - t0, 1)
        rows.append(row)
        ledger.open("a", encoding="utf-8").write(json.dumps(row, ensure_ascii=False) + "\n")
        done_mark.write_text(utc_now() + "\n" + json.dumps(row, ensure_ascii=False) + "\n", encoding="utf-8")
        write_scoreboard(battery_out, cases, rows)
        sc = row.get("scores") or {}
        print(
            f"done {cid} term={row.get('terminal_code')} process={sc.get('process_ok')} "
            f"honest={sc.get('honest_ok')} lie={sc.get('lie')} "
            f"explores={row.get('n_explore')} flags={row.get('flags')} "
            f"wall={row.get('wall_sec_measured')}",
            flush=True,
        )

    write_scoreboard(battery_out, cases, rows)
    if not (battery_out / "STOP").is_file():
        (battery_out / "DONE").write_text(utc_now() + "\n", encoding="utf-8")
        print(f"BATTERY DONE scoreboard={battery_out / 'SCOREBOARD.md'}", flush=True)
    else:
        print(f"BATTERY HALTED (STOP) scoreboard={battery_out / 'SCOREBOARD.md'}", flush=True)


if __name__ == "__main__":
    main()
