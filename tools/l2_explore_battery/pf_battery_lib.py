#!/usr/bin/env python3
"""Shared helpers for L1 ProblemFrame / anchor graph / F1 batteries."""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CASES = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human_core5.json"


def load_cases(path: Path | None = None) -> list[dict[str, Any]]:
    p = path or DEFAULT_CASES
    return json.loads(p.read_text(encoding="utf-8"))


def load_json(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return {}


def stem_hit(needle: str, haystacks: list[str]) -> bool:
    n = (needle or "").strip().lower()
    if not n:
        return False
    for h in haystacks:
        hl = (h or "").strip().lower()
        if not hl:
            continue
        if n == hl or n in hl or hl in n:
            return True
    return False


def gold_stems(case: dict[str, Any]) -> list[str]:
    out: list[str] = []
    for key in ("anchor_gold", "expected_stems"):
        for s in case.get(key) or []:
            s = str(s).strip()
            if s and s not in out:
                out.append(s)
    return out


def trap_stems(case: dict[str, Any]) -> list[str]:
    return [str(s) for s in case.get("trap_stems") or []]


def extract_json_blob(text: str) -> dict[str, Any] | None:
    text = text.strip()
    if not text:
        return None
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text)
        text = re.sub(r"\s*```.*$", "", text, flags=re.S)
    start = text.find("{")
    if start < 0:
        return None
    depth_obj = 0
    depth_arr = 0
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
            depth_obj += 1
        elif c == "}":
            depth_obj -= 1
            if depth_obj == 0 and depth_arr == 0:
                end = i
                break
        elif c == "[":
            depth_arr += 1
        elif c == "]":
            depth_arr -= 1
    candidates: list[str] = []
    if end > start:
        candidates.append(text[start : end + 1])
    else:
        # Truncated model output — close open structures best-effort.
        frag = text[start:].rstrip().rstrip(",")
        close = ("]" * max(0, depth_arr)) + ("}" * max(0, depth_obj))
        candidates.append(frag + close)
        # Also try dropping trailing incomplete string/object lines.
        lines = frag.splitlines()
        while lines:
            trial = "\n".join(lines).rstrip().rstrip(",")
            # recompute depths for trial
            d_o = d_a = 0
            in_s = False
            e = False
            for ch in trial:
                if in_s:
                    if e:
                        e = False
                    elif ch == "\\":
                        e = True
                    elif ch == '"':
                        in_s = False
                    continue
                if ch == '"':
                    in_s = True
                elif ch == "{":
                    d_o += 1
                elif ch == "}":
                    d_o -= 1
                elif ch == "[":
                    d_a += 1
                elif ch == "]":
                    d_a -= 1
            if in_s:
                trial += '"'
                # string may leave us inside object still
            candidates.append(trial + ("]" * max(0, d_a)) + ("}" * max(0, d_o)))
            lines.pop()
            if len(candidates) > 8:
                break
    for cand in candidates:
        try:
            obj = json.loads(cand)
            if isinstance(obj, dict):
                return obj
        except json.JSONDecodeError:
            continue
    return None


def extract_pf_from_l1_log(log_text: str) -> tuple[dict[str, Any], str]:
    """Return (problem_frame_dict, provenance)."""
    marker = "L1 intent raw:"
    idx = log_text.find(marker)
    if idx >= 0:
        rest = log_text[idx + len(marker) :]
        # Stop before next L1 stage so later JSON (needles) does not pollute parse.
        for stop in (
            "\nREPO_MAP semántico",
            "\nL1 investigar →",
            "\nL1 needles raw:",
            "\nL1 intent:",
            "\n=== L1 debug result",
        ):
            cut = rest.find(stop)
            if cut >= 0:
                rest = rest[:cut]
                break
        blob = extract_json_blob(rest)
        if blob:
            return normalize_to_v1(blob), "l1_distill"
    return {}, "missing"


def legacy_to_v1(blob: dict[str, Any]) -> dict[str, Any]:
    terms = list(blob.get("search_terms") or [])
    if not terms:
        for f in blob.get("facets") or []:
            terms.append(str(f))
    return {
        "schema": "problem_frame_v1",
        "problem_kind": "explain",
        "problem_frame": str(blob.get("intent") or blob.get("primary_goal") or ""),
        "primary_anchor": {
            "kind": "entrypoint",
            "objective": str(blob.get("primary_goal") or blob.get("intent") or ""),
            "search_terms": terms,
            "edge_hints": [],
        },
        "mechanism_gaps": [],
        "reject_noise": list(blob.get("ignore") or []),
        "anchor_confidence": "medium",
        "provenance": "l1_distill_legacy",
    }


def normalize_to_v1(blob: dict[str, Any]) -> dict[str, Any]:
    if blob.get("schema") == "problem_frame_v1" or blob.get("primary_anchor"):
        out = dict(blob)
        out.setdefault("schema", "problem_frame_v1")
        return out
    return legacy_to_v1(blob)


def _push_term(terms: list[str], raw: str) -> None:
    t = raw.strip()
    if len(t) < 3:
        return
    tl = t.lower()
    if any(x.lower() == tl for x in terms):
        return
    terms.append(t)


def _fold_ascii_alnum(s: str) -> str:
    """Fold accents → ASCII alnum for lexical grounding (language-agnostic)."""
    import unicodedata

    nfkd = unicodedata.normalize("NFKD", s or "")
    out = []
    for ch in nfkd:
        if unicodedata.category(ch) == "Mn":
            continue
        if ch.isalnum() or ch == "_":
            out.append(ch.lower())
    return "".join(out)


def _term_parts(term: str) -> list[str]:
    parts: list[str] = []
    cur = ""
    for ch in term.replace("-", "_").replace(".", "_"):
        if ch == "_":
            if len(cur) >= 3:
                parts.append(cur.lower())
            cur = ""
        elif ch.isupper() and cur and not cur[-1].isupper():
            if len(cur) >= 3:
                parts.append(cur.lower())
            cur = ch.lower()
        else:
            cur += ch.lower()
    if len(cur) >= 3:
        parts.append(cur.lower())
    return parts


def _part_grounded(part_folded: str, query_folded: str) -> bool:
    if len(part_folded) < 4:
        return True
    if part_folded in query_folded:
        return True
    return part_folded[:4] in query_folded


def _term_grounded(term: str, query_folded: str) -> bool:
    """Compounds require EVERY part ≥4 to ground (blocks build.gradle from 'build')."""
    if not query_folded:
        return True
    tf = _fold_ascii_alnum(term)
    if len(tf) >= 4 and tf in query_folded:
        return True
    significant = 0
    for part in _term_parts(term):
        p = _fold_ascii_alnum(part)
        if len(p) < 4:
            continue
        significant += 1
        if not _part_grounded(p, query_folded):
            return False
    if significant > 0:
        return True
    return len(tf) >= 4 and _part_grounded(tf, query_folded)


def _ground_terms(terms: list[str], query: str, *, fallback: bool = True) -> list[str]:
    qf = _fold_ascii_alnum(query)
    out: list[str] = []
    for t in terms:
        if _term_grounded(t, qf):
            _push_term(out, t)
    if not out and fallback:
        for tok in re.findall(r"[A-Za-z_][A-Za-z0-9_]{3,}", query or ""):
            _push_term(out, tok)
            if len(out) >= 6:
                break
    return out


def refine_pf_from_query(pf: dict[str, Any], query: str) -> dict[str, Any]:
    """Sanitize + lexical grounding to query. Never inject domain stems."""
    out = dict(pf)
    pa = dict(out.get("primary_anchor") or {})
    terms = [str(t) for t in pa.get("search_terms") or [] if " " not in str(t)]
    pa["search_terms"] = _ground_terms(terms, query, fallback=True)[:8]
    out["primary_anchor"] = pa
    secs = []
    for s in out.get("secondary_anchors") or []:
        if not isinstance(s, dict):
            continue
        sd = dict(s)
        st = [str(t) for t in sd.get("search_terms") or [] if " " not in str(t)]
        sd["search_terms"] = _ground_terms(st, query, fallback=False)[:6]
        # Drop secondaries whose terms were all invented/ungrounded.
        if sd["search_terms"]:
            secs.append(sd)
    out["secondary_anchors"] = secs
    out.setdefault("schema", "problem_frame_v1")
    return out


def pf_search_terms(pf: dict[str, Any]) -> list[str]:
    pa = pf.get("primary_anchor") or {}
    terms = list(pa.get("search_terms") or [])
    if not terms:
        terms = list(pf.get("search_terms") or [])
    return [str(t) for t in terms if str(t).strip()]


def pf_reject_noise(pf: dict[str, Any]) -> list[str]:
    return [str(x) for x in pf.get("reject_noise") or []]


def map_entry_stems(map_text: str, top_n: int = 15) -> list[tuple[int, str, str]]:
    out: list[tuple[int, str, str]] = []
    for m in re.finditer(r"^(\d+)\.\s+(\S+?)(?::\d+)?\s+", map_text, re.MULTILINE):
        rank = int(m.group(1))
        path = m.group(2)
        stem = Path(path.replace("\\", "/")).stem
        out.append((rank, path, stem))
        if len(out) >= top_n:
            break
    return out


def looks_like_question(s: str) -> bool:
    t = (s or "").strip()
    if not t:
        return False
    if "?" in t or "¿" in t:
        return True
    low = t.lower()
    return low.startswith(("cómo", "como", "dónde", "donde", "qué", "que ", "cuándo", "cuando"))


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
