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
    # Strip markdown fences if present.
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text)
        text = re.sub(r"\s*```$", "", text)
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end <= start:
        return None
    try:
        return json.loads(text[start : end + 1])
    except json.JSONDecodeError:
        return None


def extract_pf_from_l1_log(log_text: str) -> tuple[dict[str, Any], str]:
    """Return (problem_frame_dict, provenance)."""
    for line in log_text.splitlines():
        if "L1 intent raw:" in line:
            raw = line.split("L1 intent raw:", 1)[1].strip()
            blob = extract_json_blob(raw)
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
