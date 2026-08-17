#!/usr/bin/env python3
"""Replay failed L2 Search/Replace hunks from session.md — no LLM required.

Walks battery round dirs, extracts edit_feedback search/replace, and tries to
apply them against clean product files (git HEAD or --root).

Use this on a machine that cannot run the 7B model to see whether the current
harness would accept hunks the 7B already emitted.

  python3 tools/l2_battery/replay_failed_hunks.py
  python3 tools/l2_battery/replay_failed_hunks.py --round-dir .tuide/ai/l2_phase_e_hard2
  python3 tools/l2_battery/replay_failed_hunks.py --cli build/l2_harness_cli
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

FEEDBACK_RE = re.compile(
    r"### turn \d+ — (?:edit_feedback|edit_repeat_pushback)\n\n"
    r"(?P<body>.*?)(?=\n### turn |\Z)",
    re.S,
)
PATH_RE = re.compile(r"^path: `([^`]+)`", re.M)
SEARCH_RE = re.compile(r"## search \(failed\)\n\n```\n(.*?)```", re.S)
REPLACE_RE = re.compile(r"## replace \(intended\)\n\n```\n(.*?)```", re.S)
SHAPE_RE = re.compile(r"hunk mal formado|search demasiado genérico|search no encontrado")


def flex_normalize(s: str) -> str:
    """Port of search_replace.cpp flex_normalize (text only, no byte map)."""
    out: list[str] = []
    i = 0
    n = len(s)

    def skip_eol(j: int) -> int:
        if j >= n:
            return j
        if s[j] == "\r":
            j += 1
            if j < n and s[j] == "\n":
                j += 1
            return j
        if s[j] == "\n":
            return j + 1
        return j

    while i < n:
        line_start = i
        content_end = i
        has = False
        while i < n and s[i] not in "\n\r":
            if s[i] not in " \t":
                has = True
                content_end = i + 1
            i += 1
        eol_at = i
        if has:
            out.append(s[line_start:content_end])
            if eol_at < n:
                out.append("\n")
                i = skip_eol(eol_at)
                while i < n:
                    t = i
                    while t < n and s[t] in " \t":
                        t += 1
                    if t < n and s[t] in "\n\r":
                        i = skip_eol(t)
                        continue
                    break
        elif eol_at < n:
            i = skip_eol(eol_at)
        else:
            break
    return "".join(out)


def find_unique(haystack: str, needle: str) -> str:
    if not needle:
        return "empty"
    first = haystack.find(needle)
    if first < 0:
        return "0 matches"
    second = haystack.find(needle, first + len(needle))
    if second >= 0:
        return "ambiguous"
    return "exact"


def find_unique_flex(haystack: str, needle: str) -> str:
    h = flex_normalize(haystack)
    n = flex_normalize(needle)
    if not n:
        return "empty"
    first = h.find(n)
    if first < 0:
        return "0 matches"
    second = h.find(n, first + len(n))
    if second >= 0:
        return "ambiguous"
    return "flex"


def first_content_line(text: str) -> str:
    for ln in text.splitlines():
        t = ln.strip()
        if t:
            return t
    return ""


def opener_only(search: str) -> bool:
    s = search.strip()
    if not s:
        return False
    lines = [ln for ln in s.splitlines() if ln.strip()]
    has_open = "{" in s
    has_close = "}" in s
    return (has_open and not has_close and len(lines) <= 2) or s.endswith("{") and len(lines) <= 2


def looks_like_type_opener(search: str) -> bool:
    line = first_content_line(search)
    return line.startswith(("struct ", "class ", "enum ", "union "))


def looks_like_new_api(added: str) -> bool:
    t = added.strip()
    if len(t) < 8:
        return False
    if ") {" in t or "){" in t:
        return True
    for ln in added.splitlines():
        s = ln.strip()
        if len(s) >= 6 and s.endswith(";") and "(" in s and "=" not in s:
            return True
    return False


def classify_shape(search: str, replace: str) -> str:
    if not search.strip() or len(search.strip()) < 12:
        return "too_generic"
    if replace.startswith(search) or replace.strip().startswith(search.strip()):
        added = replace[len(search) :] if replace.startswith(search) else replace.strip()[len(search.strip()) :]
        if looks_like_new_api(added):
            if looks_like_type_opener(search):
                return "opener_struct"
            return "insert_ok"
    if opener_only(search) and looks_like_type_opener(search):
        return "opener_struct"
    if opener_only(search):
        return "opener_other"
    return "ok"


def extract_hunks(session_text: str) -> list[dict]:
    out = []
    for m in FEEDBACK_RE.finditer(session_text):
        body = m.group("body")
        pm = PATH_RE.search(body)
        sm = SEARCH_RE.search(body)
        rm = REPLACE_RE.search(body)
        if not sm:
            continue
        err = ""
        em = SHAPE_RE.search(body)
        if em:
            err = body.split("\n", 1)[0][:200]
        out.append(
            {
                "path": pm.group(1) if pm else "",
                "search": sm.group(1),
                "replace": rm.group(1) if rm else "",
                "session_error": err,
            }
        )
    return out


def load_file(root: Path, rel: str, use_git: bool) -> str:
    if use_git:
        r = subprocess.run(
            ["git", "show", f"HEAD:{rel}"],
            cwd=root,
            capture_output=True,
            text=True,
        )
        if r.returncode == 0:
            return r.stdout
    p = root / rel
    if p.exists():
        return p.read_text(errors="replace")
    return ""


def try_cli(cli: Path, root: Path, path: str, search: str, replace: str, text: str) -> dict:
    payload = {"path": path, "search": search, "replace": replace, "text": text}
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        json.dump(payload, f)
        tmp = f.name
    try:
        r = subprocess.run(
            [str(cli), "hunk-try", tmp],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        line = (r.stdout or r.stderr or "").strip().splitlines()
        last = line[-1] if line else ""
        try:
            return json.loads(last)
        except json.JSONDecodeError:
            return {"ok": False, "error": last or f"exit {r.returncode}"}
    finally:
        Path(tmp).unlink(missing_ok=True)


def default_round_dirs(root: Path) -> list[Path]:
    base = root / ".tuide" / "ai"
    if not base.exists():
        return []
    out = []
    for pat in ("l2_phase_*", "l2_overnight", "l2_prompt_sweep*"):
        out.extend(sorted(base.glob(pat)))
    return [p for p in out if p.is_dir()]


def iter_sessions(round_dir: Path) -> list[Path]:
    return sorted(round_dir.glob("*/session.md"))


def verdict(row: dict) -> str:
    if row.get("cli_ok") is True:
        return "would_apply_cli"
    if row.get("match") in ("exact", "flex") and row.get("shape") in ("ok", "insert_ok"):
        return "would_apply_py"
    if row.get("shape") == "opener_struct":
        return "blocked_opener_struct"
    if row.get("shape") == "opener_other":
        return "blocked_opener"
    if row.get("match") == "0 matches":
        return "no_match"
    if row.get("match") == "ambiguous":
        return "ambiguous"
    if not row.get("file_found"):
        return "missing_file"
    return "other"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=ROOT, help="repo root (product files)")
    ap.add_argument(
        "--round-dir",
        action="append",
        type=Path,
        dest="round_dirs",
        help="battery round dir (repeatable). Default: .tuide/ai/l2_phase_* + overnight + sweeps",
    )
    ap.add_argument("--cli", type=Path, default=None, help="optional build/l2_harness_cli for C++ hunk-try")
    ap.add_argument("--no-git", action="store_true", help="read files from disk, not git HEAD")
    ap.add_argument("--jsonl", type=Path, default=None, help="write per-hunk JSONL")
    args = ap.parse_args()
    root: Path = args.root.resolve()
    dirs = [p.resolve() for p in (args.round_dirs or default_round_dirs(root))]
    if not dirs:
        print("no round dirs found (pass --round-dir)", file=sys.stderr)
        return 2

    cli = args.cli
    if cli and not cli.is_file():
        print(f"warn: --cli not found ({cli}), python-only", file=sys.stderr)
        cli = None

    rows = []
    for rd in dirs:
        for sess in iter_sessions(rd):
            text = sess.read_text(errors="replace")
            hunks = extract_hunks(text)
            case = sess.parent.name
            for h in hunks:
                rel = h["path"]
                body = load_file(root, rel, use_git=not args.no_git) if rel else ""
                exact = find_unique(body, h["search"]) if body else "missing"
                flex = find_unique_flex(body, h["search"]) if body else "missing"
                if exact == "exact":
                    match = "exact"
                elif flex == "flex":
                    match = "flex"
                else:
                    match = flex if body else "missing"
                shape = classify_shape(h["search"], h["replace"])
                row = {
                    "round": str(rd),
                    "case": case,
                    "path": rel,
                    "search_preview": h["search"][:80].replace("\n", "\\n"),
                    "match": match,
                    "shape": shape,
                    "file_found": bool(body),
                    "session_error": h["session_error"],
                    "cli_ok": None,
                    "cli_error": "",
                }
                if cli and body:
                    cr = try_cli(cli, root, rel, h["search"], h["replace"], body)
                    row["cli_ok"] = bool(cr.get("ok"))
                    row["cli_error"] = str(cr.get("error") or "")[:160]
                row["verdict"] = verdict(row)
                rows.append(row)

    counts = Counter(r["verdict"] for r in rows)
    print(f"hunks={len(rows)} rounds={len(dirs)}")
    for k, n in counts.most_common():
        print(f"  {k}: {n}")
    print("\nby case:")
    by_case: dict[str, Counter] = {}
    for r in rows:
        key = f"{r['round']}/{r['case']}"
        by_case.setdefault(key, Counter())[r["verdict"]] += 1
    for key in sorted(by_case):
        bits = ", ".join(f"{k}={v}" for k, v in by_case[key].most_common())
        print(f"  {key}: {bits}")

    out_path = args.jsonl
    if out_path is None:
        out_path = root / ".tuide" / "ai" / "hunk_replay.jsonl"
    try:
        out_path.parent.mkdir(parents=True, exist_ok=True)
        with out_path.open("w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
        print(f"\njsonl={out_path}")
    except OSError as e:
        print(f"\njsonl write skipped: {e}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
