#!/usr/bin/env python3
"""Autonomous overnight battery: probe_admin_pilot × N prompts.

Sequential. Skips cases with DONE. Writes SCOREBOARD.json at the end.
Does not touch wave-explore / sense.
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
    except Exception:  # noqa: BLE001
        return False


def score_case(case: dict, summary: dict | None, err: str | None) -> dict:
    gold = case.get("gold") or {}
    out: dict = {
        "id": case["id"],
        "kind": case.get("kind"),
        "difficulty": case.get("difficulty"),
        "style": case.get("style"),
        "ok": False,
        "flags": [],
        "exit_do": None,
        "n_explore": 0,
        "wall_sec": None,
        "error": err,
    }
    if summary is None:
        out["flags"].append("no_summary")
        return out

    closed = summary.get("closed") or {}
    jobs = summary.get("jobs") or []
    explores = [j for j in jobs if j.get("tipo") == "explore"]
    out["n_explore"] = len(explores)
    out["wall_sec"] = summary.get("wall_sec")
    out["exit_do"] = closed.get("do")
    out["exit_kind"] = closed.get("kind")
    out["falta_pilot"] = (closed.get("falta") or "")[:200]
    out["cubre_pilot"] = (closed.get("cubre") or "")[:200]
    out["reply_head"] = (closed.get("reply") or "")[:240]
    out["job_verdicts"] = [j.get("veredicto") for j in explores]
    out["job_faltas"] = [j.get("falta") for j in explores]

    blob = " ".join(
        [
            str(closed.get("why") or ""),
            str(closed.get("reply") or ""),
            str(closed.get("cubre") or ""),
            str(closed.get("falta") or ""),
        ]
    ).lower()

    out["verify_reports"] = [
        {
            "trigger": r.get("trigger"),
            "veredicto": r.get("veredicto"),
            "why": (r.get("why") or "")[:200],
            "skipped": bool(r.get("skipped")),
            "omit_gate": bool(r.get("omit_gate")),
        }
        for r in (summary.get("verify_reports") or [])
    ]
    out["verify_blocked_any"] = any(
        (r.get("veredicto") or "") in ("refuta", "dudoso") and not r.get("omit_gate")
        for r in (summary.get("verify_reports") or [])
    )
    out["verify_last"] = (
        (summary.get("verify_reports") or [{}])[-1].get("veredicto")
        if summary.get("verify_reports")
        else None
    )
    if any(r.get("omit_gate") for r in (summary.get("verify_reports") or [])):
        out["flags"].append("verify_cap_omit")
    # Cap must never look like a successful sostienen.
    for r in summary.get("verify_reports") or []:
        if r.get("skipped") or r.get("omit_gate"):
            if (r.get("veredicto") or "").lower() == "sostiene":
                out["flags"].append("verify_cap_absuelve")
                out["ok"] = False

    expect_exit = gold.get("expect_exit")
    if expect_exit and closed.get("do") not in expect_exit:
        out["flags"].append(f"exit_unexpected:{closed.get('do')}")

    if gold.get("prefer_ask_user") and closed.get("do") != "ask_user":
        out["flags"].append("prefer_ask_user")

    if gold.get("expect_not_already_implemented"):
        bad_phrases = gold.get("false_friend_forbid_in_cubre_or_reply") or [
            "ya está implementad",
            "ya está en el código",
            "funcionalidad solicitada ya",
            "ya implementada",
        ]
        if any(p.lower() in blob for p in bad_phrases):
            out["flags"].append("false_already_implemented")
        if (closed.get("falta") or "").strip().lower() in ("nada", "ninguna", "none", ""):
            if closed.get("do") == "confirmar_cerrar" and "modific" in blob:
                out["flags"].append("falta_nada_but_will_edit")

    for p in gold.get("false_friend_forbid_in_cubre_or_reply") or []:
        if p.lower() in blob:
            out["flags"].append(f"forbid_phrase:{p[:40]}")

    symbols_any = gold.get("symbols_any") or []
    if symbols_any:
        sym_blob = " ".join(
            str(x)
            for j in explores
            for x in (j.get("simbolos") or [])
        ) + " " + " ".join(str(j.get("summary_head") or "") for j in explores)
        if not any(s.lower() in sym_blob.lower() for s in symbols_any):
            out["flags"].append("symbols_miss")

    if gold.get("expect_atomize_or_multi_explore") and len(explores) < 2:
        out["flags"].append("no_atomize")

    # Soft pass: finished without crash and no hard false-already on hole cases
    hard = {"false_already_implemented", "no_summary"}
    out["ok"] = err is None and not (hard & set(out["flags"]))
    if gold.get("prefer_ask_user") and closed.get("do") == "ask_user":
        out["ok"] = True
        out["flags"] = [f for f in out["flags"] if f != "prefer_ask_user"]
    return out


def write_scoreboard(battery_out: Path, cases: list[dict], rows: list[dict]) -> None:
    by_kind: dict[str, dict] = {}
    by_diff: dict[str, dict] = {}
    for r in rows:
        for key, bucket in (("kind", by_kind), ("difficulty", by_diff)):
            k = str(r.get(key) or "?")
            b = bucket.setdefault(k, {"n": 0, "ok": 0, "flags": {}})
            b["n"] += 1
            if r.get("ok"):
                b["ok"] += 1
            for f in r.get("flags") or []:
                b["flags"][f] = b["flags"].get(f, 0) + 1

    board = {
        "generated_at": utc_now(),
        "n_cases": len(cases),
        "n_scored": len(rows),
        "ok_frac": (sum(1 for r in rows if r.get("ok")) / len(rows)) if rows else 0.0,
        "by_kind": by_kind,
        "by_difficulty": by_diff,
        "rows": rows,
        "weak_flags": sorted(
            (
                (f, sum(1 for r in rows if f in (r.get("flags") or [])))
                for f in {x for r in rows for x in (r.get("flags") or [])}
            ),
            key=lambda t: -t[1],
        ),
    }
    (battery_out / "SCOREBOARD.json").write_text(
        json.dumps(board, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    # Human one-pager
    lines = [
        f"# Admin explore battery scoreboard ({utc_now()})",
        f"ok_frac={board['ok_frac']:.2f}  scored={board['n_scored']}/{board['n_cases']}",
        "",
        "## By kind",
    ]
    for k, b in sorted(by_kind.items()):
        lines.append(f"- {k}: {b['ok']}/{b['n']}")
    lines.append("")
    lines.append("## Weak flags")
    for f, n in board["weak_flags"][:12]:
        lines.append(f"- {f}: {n}")
    lines.append("")
    lines.append("## Cases")
    for r in rows:
        lines.append(
            f"- {r['id']}: ok={r['ok']} exit={r.get('exit_do')} "
            f"explores={r.get('n_explore')} verify={r.get('verify_last')} "
            f"blocked={r.get('verify_blocked_any')} flags={r.get('flags')}"
        )
    (battery_out / "SCOREBOARD.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--cases",
        default=str(Path(__file__).resolve().parent / "admin_explore_battery_cases.json"),
    )
    ap.add_argument("--out", required=True)
    ap.add_argument("--api", default="http://192.168.64.1:8080/v1")
    ap.add_argument("--model", default="qwen3.6-27b-q8_0")
    ap.add_argument("--max-turns", type=int, default=16)
    ap.add_argument("--max-explore", type=int, default=4)
    ap.add_argument("--only", default="", help="comma ids to run (debug)")
    args = ap.parse_args()

    battery_out = Path(args.out)
    battery_out.mkdir(parents=True, exist_ok=True)
    cases = json.loads(Path(args.cases).read_text(encoding="utf-8"))["cases"]
    if args.only.strip():
        want = {x.strip() for x in args.only.split(",") if x.strip()}
        cases = [c for c in cases if c["id"] in want]

    ledger = battery_out / "ledger.jsonl"
    rows: list[dict] = []
    # Resume prior scores if present
    prev_board = battery_out / "SCOREBOARD.json"
    done_ids = set()
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

        # Wait for API if briefly down
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

        row = score_case(case, summary, err)
        row["wall_sec_measured"] = round(time.time() - t0, 1)
        rows.append(row)
        ledger.open("a", encoding="utf-8").write(json.dumps(row, ensure_ascii=False) + "\n")
        done_mark.write_text(utc_now() + "\n" + json.dumps(row, ensure_ascii=False) + "\n", encoding="utf-8")
        write_scoreboard(battery_out, cases, rows)
        print(
            f"done {cid} ok={row['ok']} exit={row.get('exit_do')} "
            f"explores={row.get('n_explore')} flags={row.get('flags')} "
            f"wall={row.get('wall_sec_measured')}",
            flush=True,
        )

    (battery_out / "DONE").write_text(utc_now() + "\n", encoding="utf-8")
    write_scoreboard(battery_out, cases, rows)
    print(f"BATTERY DONE scoreboard={battery_out / 'SCOREBOARD.md'}", flush=True)


if __name__ == "__main__":
    main()
