#!/usr/bin/env python3
"""Probe sintético de un worker (cubre|como|gap), sin piloto ni plenario.

  python3 tools/l2_worker_probe.py --kind cubre --cycle 1
  python3 tools/l2_worker_probe.py --pack robustness --cycle 1
  python3 tools/l2_worker_probe.py --pack items --cycle 0 --from .tuide/ai/l2_explore_battery/worker_tune/robustness/cycle_3
  python3 tools/l2_worker_probe.py --pack synth --cycle 1
  python3 tools/l2_worker_probe.py --pack tree --cycle 1 --label tree_accum
  python3 tools/l2_worker_probe.py --pack items --compare 0,1
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/l2_harness_cli"
CASES_PATH = ROOT / "tools/l2_battery/worker_probe/cases.json"
ROBUST_PATH = ROOT / "tools/l2_battery/worker_probe/robustness.json"
ITEMS_PATH = ROOT / "tools/l2_battery/worker_probe/items.json"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
TUNE_ROOT = ROOT / ".tuide/ai/l2_explore_battery/worker_tune"

TEMPLATE_NEEDLES = (
    "append_top_level_tabs_header",
    "la ficha muestra el ui pedido",
    "trigger→estado→efecto",
    "trigger->estado->efecto",
    "path.cpp:fn",
    ":fn",
    "hace x",
    "sym:",
)


def env_base() -> dict[str, str]:
    e = os.environ.copy()
    e["TUIDE_ROOT"] = str(ROOT)
    return e


def load_pack(name: str) -> dict:
    if name == "robustness":
        path = ROBUST_PATH
    elif name == "items":
        path = ITEMS_PATH
    elif name == "synth":
        path = ROOT / "tools/l2_battery/worker_probe/synth.json"
    elif name == "tree":
        path = ROOT / "tools/l2_battery/worker_probe/tree.json"
    else:
        path = CASES_PATH
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_case_dir(cards_root: Path, case_id: str) -> Path:
    direct = cards_root / case_id
    if (direct / "inspect.json").exists() or (direct / "judge_cards.json").exists():
        return direct
    survey = cards_root / "survey" / case_id
    if (survey / "inspect.json").exists():
        return survey
    cards = cards_root / "cards" / case_id
    if (cards / "judge_cards.json").exists():
        return cards
    return direct


def load_payload(case_dir: Path) -> dict:
    for name in ("inspect.json", "judge_cards.json"):
        p = case_dir / name
        if p.exists():
            return json.loads(p.read_text(encoding="utf-8"))
    return {}


def zone_for_stem(payload: dict, stem: str) -> dict | None:
    needle = stem.lower()
    for z in payload.get("zones") or []:
        if not isinstance(z, dict):
            continue
        if str(z.get("stem") or "").lower() == needle:
            return z
        for key in ("primary_stems", "core_stems", "context_stems"):
            for s in z.get(key) or []:
                if str(s).lower() == needle:
                    return z
    return None


def zone_targets(zone: dict) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()

    def push(v: object) -> None:
        if isinstance(v, str) and v and v not in seen and v != "?":
            seen.add(v)
            out.append(v)
        elif isinstance(v, dict):
            for k in ("target", "from", "to", "path_symbol", "symbol"):
                if v.get(k):
                    push(v.get(k))

    for r in zone.get("representatives") or []:
        push(r)
    for e in zone.get("edges") or []:
        push(e)
    for e in zone.get("ports") or []:
        push(e)
    mech = zone.get("mechanism") or {}
    if isinstance(mech, dict):
        for slot in ("trigger", "state", "effect"):
            push(mech.get(slot))
    return out


def zone_is_thin(zone: dict) -> bool:
    if zone_targets(zone):
        return False
    if zone.get("edges") or zone.get("nuclei"):
        return False
    reps = zone.get("representatives") or []
    return len(reps) == 0


def symbol_key(target: str) -> str:
    s = target.lower()
    if "::" in s:
        s = s.rsplit("::", 1)[-1]
    if ":" in s:
        after = s.rsplit(":", 1)[-1]
        if after and "/" not in after:
            s = after
    s = re.sub(r"\[.*", "", s)
    return s.strip()


def cites_card(text: str, targets: list[str]) -> bool:
    hay = (text or "").lower()
    for t in targets:
        sk = symbol_key(t)
        if len(sk) >= 5 and sk in hay:
            return True
    return False


def is_template(text: str) -> bool:
    hay = (text or "").lower()
    return any(n in hay for n in TEMPLATE_NEEDLES)


def find_thin_zone(payload: dict) -> tuple[str, dict] | None:
    for z in payload.get("zones") or []:
        if not isinstance(z, dict) or not zone_is_thin(z):
            continue
        stems = [str(s) for s in (z.get("primary_stems") or []) if s]
        stem = stems[0] if stems else str(z.get("id") or "")
        if stem:
            return stem, z
    return None


def raw_informe_verdict(out_dir: Path) -> str:
    raws = sorted(out_dir.glob("w1_raw_*.txt"))
    for p in reversed(raws):
        text = p.read_text(encoding="utf-8", errors="replace")
        if "causal_pilot_worker_v1" not in text:
            continue
        m = re.search(r'"verdict"\s*:\s*"([^"]+)"', text)
        if m:
            return m.group(1).split("|")[0].strip().lower()
    return ""


def collect_tools(out_dir: Path) -> list[dict]:
    tools = []
    for p in sorted(out_dir.glob("w1_tool_*.md")):
        body = p.read_text(encoding="utf-8", errors="replace")
        m = re.search(r"----- (\S+) (\S+)", body)
        tools.append(
            {
                "file": p.name,
                "tool": m.group(1) if m else "",
                "target": m.group(2) if m else "",
                "body": body,
            }
        )
    return tools


def _repeat_tool(tools: list[dict]) -> bool:
    keys = [
        symbol_key(t.get("target") or "")
        for t in tools
        if t.get("tool") in ("follow", "need_code") and t.get("target")
    ]
    keys = [k for k in keys if k]
    return len(keys) != len(set(keys))


def ident_bounded(hay: str, needle: str) -> bool:
    if not needle or not hay:
        return False
    return (
        re.search(
            r"(?<![A-Za-z0-9_])" + re.escape(needle.lower()) + r"(?![A-Za-z0-9_])",
            hay.lower(),
        )
        is not None
    )


def walk_claimed_text(walk: str, chain: str = "") -> str:
    text = walk or chain or ""
    parts = re.split(r"\s*(?:->|→|;)\s*", text)
    claimed: list[str] = []
    for part in parts:
        pl = part.strip().lower()
        if not pl or pl.startswith("duda:") or pl.startswith("duda ") or pl.startswith(
            "port_to:"
        ) or pl.startswith("port_to "):
            continue
        claimed.append(pl)
    return " ".join(claimed)


def tool_discovered_keys(tools: list[dict]) -> set[str]:
    keys: set[str] = set()
    for t in tools:
        body = str(t.get("body") or "")
        for m in re.finditer(r"fn=`([^`]+)`", body):
            sk = symbol_key(m.group(1))
            if sk:
                keys.add(sk)
    return keys


def tag_tool(target: str, zone: dict, tool: str, kind: str, extra_keys: set[str] | None = None) -> str:
    if kind == "cubre" and tool == "follow":
        return "fuera_de_modo"
    if tool == "dataflow":
        return "de_dataflow"
    keys = {symbol_key(t) for t in zone_targets(zone)}
    if extra_keys:
        keys |= extra_keys
    sk = symbol_key(target)
    if sk and sk in keys:
        return "de_ficha"
    if tool == "outline":
        return "de_outline"
    if tool == "causal":
        return "inventada" if target else "de_ficha"
    if target:
        return "inventada"
    return "de_ficha"


def score_row(case: dict, summary: dict, out_dir: Path, zone: dict, thin_applied: bool) -> dict:
    tools = collect_tools(out_dir)
    why = str(summary.get("why") or "")
    brief = str(summary.get("brief") or "")
    if not brief and why:
        brief = why
    walk = str(summary.get("walk") or "")
    chain = str(summary.get("chain") or "")
    if not walk:
        walk = chain
    path_symbol = str(summary.get("path_symbol") or "")
    verdict = str(summary.get("verdict") or "")
    raw_verdict = raw_informe_verdict(out_dir)
    ok = bool(summary.get("ok")) and not bool(summary.get("is_tool"))
    targets = zone_targets(zone)
    expect = case.get("expect") or {}
    want = [str(v) for v in (expect.get("verdict") or [])]
    template = (
        is_template(why)
        or is_template(brief)
        or is_template(chain)
        or is_template(walk)
        or is_template(path_symbol)
    )
    cited = (
        cites_card(why + " " + brief + " " + chain + " " + walk + " " + path_symbol, targets)
        if targets
        else bool(tools)
    )
    brief_ok = len(brief) >= 24 and " " in brief
    extra_keys = tool_discovered_keys(tools)
    tags = [
        tag_tool(t.get("target") or "", zone, t.get("tool") or "", case["kind"], extra_keys)
        for t in tools
    ]
    invented = "inventada" in tags
    fuera = "fuera_de_modo" in tags
    notes: list[str] = []
    pass_ok = True
    if not ok:
        pass_ok = False
        notes.append("informe_invalido:" + str(summary.get("error") or ""))
    if want and verdict not in want:
        pass_ok = False
        notes.append(f"verdict={verdict} want={want}")
    if case.get("trap") and (verdict == "cubre" or raw_verdict == "cubre"):
        pass_ok = False
        notes.append("trap_lie")
        if raw_verdict == "cubre" and verdict != "cubre":
            notes.append("parse_flip")
    tools_used = {str(t.get("tool") or "") for t in tools}
    if case.get("require_read"):
        if "need_code" not in tools_used:
            pass_ok = False
            notes.append("sin_need_code")
        if "dataflow" not in tools_used and "causal" not in tools_used:
            pass_ok = False
            notes.append("sin_flujo")
        if tools and tools_used <= {"follow", "outline"}:
            pass_ok = False
            notes.append("solo_aristas")
    if thin_applied and case.get("require_tool") and not tools:
        pass_ok = False
        notes.append("thin_sin_tool")
    if case["kind"] in ("como", "gap") and case.get("require_tool") and not tools:
        pass_ok = False
        notes.append("sin_tool")
    if template:
        pass_ok = False
        notes.append("plantilla")
    if invented:
        pass_ok = False
        notes.append("inventada")
    if fuera:
        pass_ok = False
        notes.append("fuera_de_modo")
    if ok and targets and not cited and verdict in ("cubre", "chain", "found"):
        pass_ok = False
        notes.append("why_sin_simbolo")
    if ok and not brief_ok:
        pass_ok = False
        notes.append("brief_corto")
    gold = [str(g) for g in (case.get("gold_writers") or [])]
    claimed = walk_claimed_text(walk, chain)
    if gold:
        missing = [g for g in gold if not ident_bounded(claimed, g)]
        if missing:
            pass_ok = False
            notes.append("sin_writer:" + ",".join(missing))
        if ident_bounded(claimed, "tick") and not ident_bounded(claimed, "halt_busy"):
            pass_ok = False
            notes.append("hop_ficha")
    gold_fields = [str(g) for g in (case.get("gold_fields") or [])]
    hay_fields = " ".join([walk, brief, why])
    if gold_fields:
        missing_f = [g for g in gold_fields if not ident_bounded(hay_fields, g)]
        if missing_f:
            pass_ok = False
            notes.append("sin_campo:" + ",".join(missing_f))
    gold_clearers = [str(g) for g in (case.get("gold_clearers") or [])]
    if gold_clearers and not any(ident_bounded(claimed, g) for g in gold_clearers):
        pass_ok = False
        notes.append("sin_clearer")
    decoys = [str(d) for d in (case.get("decoy_writers") or [])]
    hit_decoy = [d for d in decoys if ident_bounded(claimed, d)]
    if hit_decoy:
        pass_ok = False
        notes.append("decoy:" + ",".join(hit_decoy))
    walk_l = walk.lower()
    port = str(summary.get("port_to") or "").lower()
    if case.get("require_duda") and "duda:" not in walk_l:
        pass_ok = False
        notes.append("sin_duda")
    if case.get("require_port_to") and not port and "port_to:" not in walk_l:
        pass_ok = False
        notes.append("sin_port_to")
    if case.get("require_inbound"):
        inbound_ok = ("duda:" in walk_l) or bool(port) or ("port_to:" in walk_l)
        if not inbound_ok:
            pass_ok = False
            notes.append("sin_inbound")
    min_nc = int(case.get("min_need_code") or 0)
    n_nc = sum(1 for t in tools if t.get("tool") == "need_code")
    if min_nc and n_nc < min_nc:
        pass_ok = False
        notes.append(f"need_code={n_nc}<{min_nc}")
    forbid = [str(x).lower() for x in (case.get("forbid_need_code") or [])]
    if forbid:
        bad = [
            str(t.get("target") or "")
            for t in tools
            if t.get("tool") == "need_code"
            and any(f in str(t.get("target") or "").lower() for f in forbid)
        ]
        if bad:
            pass_ok = False
            notes.append("abrio_fuera:" + ",".join(bad))
    return {
        "id": case["id"],
        "family": case.get("family") or "",
        "kind": case["kind"],
        "case_id": case["case_id"],
        "stem": summary.get("stem") or case.get("stem"),
        "zone": summary.get("zone"),
        "ok": ok,
        "pass": pass_ok,
        "trap": bool(case.get("trap")),
        "raw_verdict": raw_verdict,
        "trap_lie": bool(case.get("trap") and (verdict == "cubre" or raw_verdict == "cubre")),
        "thin_applied": thin_applied,
        "verdict": verdict,
        "covers": bool(summary.get("covers")),
        "steps": summary.get("steps"),
        "tools": [
            {
                "tool": t["tool"],
                "target": t["target"],
                "tag": tag,
                "empty_hops": "sin hops" in (t.get("body") or ""),
            }
            for t, tag in zip(tools, tags)
        ],
        "why": why,
        "brief": brief,
        "brief_ok": brief_ok,
        "walk": walk,
        "walk_claimed": claimed,
        "chain": chain,
        "gold_writers": gold,
        "port_to": summary.get("port_to") or "",
        "error": summary.get("error") or "",
        "notes": notes,
        "items": list(case.get("items") or []),
        "cited": cited,
        "parse_flip": bool(
            case.get("kind") == "cubre"
            and raw_verdict
            and verdict
            and raw_verdict != verdict
        ),
        "repeat_tool": _repeat_tool(tools),
        "empty_hops": any("sin hops" in (t.get("body") or "") for t in tools),
    }


def run_one(
    case: dict,
    cards_root: Path,
    out_dir: Path,
    env: dict[str, str],
    timeout: int,
    workspace: Path | None = None,
) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    case_dir = resolve_case_dir(cards_root, case["case_id"])
    payload = load_payload(case_dir)
    stem = str(case.get("stem") or "")
    thin_applied = False
    zone: dict = {}
    if case.get("thin"):
        hit = find_thin_zone(payload)
        if hit is None:
            return {
                "id": case["id"],
                "kind": case["kind"],
                "case_id": case["case_id"],
                "pass": True,
                "skipped": True,
                "notes": ["thin_payload_not_empty"],
                "trap": False,
                "trap_lie": False,
                "thin_applied": False,
                "tools": [],
            }
        stem, zone = hit
        thin_applied = True
        question = case.get("question") or f"¿es {stem} el objeto de la consulta?"
    else:
        z = zone_for_stem(payload, stem)
        if z is None:
            return {
                "id": case["id"],
                "pass": False,
                "notes": [f"stem_ausente:{stem}"],
                "trap": bool(case.get("trap")),
                "trap_lie": False,
                "tools": [],
            }
        zone = z
        question = case.get("question") or ""
        thin_applied = False
    cmd = [
        str(CLI),
        "worker-probe",
        "--kind",
        case["kind"],
        "--case",
        case["case_id"] or case["id"],
        "--stem",
        stem,
        "--cards-root",
        str(cards_root),
        "--out",
        str(out_dir),
    ]
    if question:
        cmd.extend(["--question", question])
    zid = str(zone.get("id") or "")
    if zid:
        cmd.extend(["--zone", zid])
    instruction = str(case.get("instruction") or "")
    if instruction:
        cmd.extend(["--instruction", instruction])
    if workspace is not None:
        cmd.extend(["--workspace", str(workspace)])
    log = out_dir / "probe.log"
    try:
        proc = subprocess.run(
            cmd,
            cwd=str(ROOT),
            env=env,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        log.write_text("timeout\n", encoding="utf-8")
        return {
            "id": case["id"],
            "pass": False,
            "notes": ["timeout"],
            "trap": bool(case.get("trap")),
            "trap_lie": False,
            "tools": [],
        }
    log.write_text((proc.stdout or "") + "\n--- stderr ---\n" + (proc.stderr or ""), encoding="utf-8")
    summary_path = out_dir / "summary.json"
    if not summary_path.exists():
        return {
            "id": case["id"],
            "pass": False,
            "notes": ["no_summary", f"rc={proc.returncode}"],
            "trap": bool(case.get("trap")),
            "trap_lie": False,
            "tools": [],
        }
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    inspect = load_payload(out_dir) or payload
    z2 = zone_for_stem(inspect, str(summary.get("stem") or stem)) or zone
    return score_row(case, summary, out_dir, z2, thin_applied)


def item_row_pass(item_id: str, row: dict) -> bool | None:
    if row.get("skipped") or item_id not in (row.get("items") or []):
        return None
    tools = row.get("tools") or []
    invented = any(t.get("tag") == "inventada" for t in tools)
    empty_hops = bool(row.get("empty_hops")) or any(t.get("empty_hops") for t in tools)
    raw = str(row.get("raw_verdict") or "")
    verdict = str(row.get("verdict") or "")
    if item_id == "follow_ficha":
        return not invented
    if item_id == "empty_follow_rep":
        if not tools:
            return False
        n_unique = n_unique_tools(tools)
        if empty_hops and verdict == "no_cubre" and n_unique <= 1:
            return False
        return True
    if item_id == "brief_as_why":
        return bool(row.get("ok")) and bool(row.get("brief_ok"))
    if item_id == "no_silent_flip":
        if not raw or not verdict:
            return False
        return raw == verdict
    if item_id == "no_repeat_follow":
        return not bool(row.get("repeat_tool"))
    if item_id == "trap_on_raw":
        return raw == "no_cubre"
    if item_id == "gold_keep":
        return bool(row.get("pass")) and raw == "cubre" and verdict == "cubre"
    if item_id == "walk_gold":
        golds = [str(g).lower() for g in (row.get("gold_writers") or [])]
        if golds:
            claimed = str(row.get("walk_claimed") or "").lower()
            full = (str(row.get("walk") or "") + " " + str(row.get("chain") or "")).lower()
            for g in golds:
                if g in claimed:
                    continue
                if f"duda:{g}" in full:
                    continue
                return False
            return True
        if row.get("trap"):
            return raw == "no_cubre"
        return bool(row.get("walk") or row.get("chain"))
    return None


def n_unique_tools(tools: list[dict]) -> int:
    return len({symbol_key(t.get("target") or "") for t in tools if t.get("target")})


def score_items(item_defs: list[dict], rows: list[dict]) -> list[dict]:
    out = []
    for spec in item_defs:
        iid = spec["id"]
        hits = []
        for row in rows:
            v = item_row_pass(iid, row)
            if v is None:
                continue
            hits.append({"id": row.get("id"), "pass": v})
        n = len(hits)
        n_pass = sum(1 for h in hits if h["pass"])
        out.append(
            {
                "id": iid,
                "title": spec.get("title") or iid,
                "n_pass": n_pass,
                "n": n,
                "reason": f"{n_pass}/{n}" if n else "0/0",
                "rows": hits,
            }
        )
    return out


def rescore_from(case: dict, from_dir: Path, cards_root: Path) -> dict:
    out_dir = from_dir / case["id"]
    if not (out_dir / "summary.json").exists():
        return {
            "id": case["id"],
            "pass": False,
            "notes": ["no_summary"],
            "items": list(case.get("items") or []),
            "tools": [],
            "trap": bool(case.get("trap")),
            "trap_lie": False,
        }
    summary = json.loads((out_dir / "summary.json").read_text(encoding="utf-8"))
    payload = load_payload(out_dir) or load_payload(resolve_case_dir(cards_root, case["case_id"]))
    stem = str(summary.get("stem") or case.get("stem") or "")
    zone = zone_for_stem(payload, stem) or {}
    row = score_row(case, summary, out_dir, zone, False)
    row["items"] = list(case.get("items") or [])
    return row


def compare_item_scores(a: dict, b: dict) -> str:
    ia = {x["id"]: x for x in (a.get("item_scores") or [])}
    ib = {x["id"]: x for x in (b.get("item_scores") or [])}
    order = [x["id"] for x in (a.get("item_scores") or [])]
    for iid in ib:
        if iid not in order:
            order.append(iid)
    lines = ["item                    before  after   delta"]
    for iid in order:
        sa, sb = ia.get(iid, {}), ib.get(iid, {})
        ra, rb = sa.get("reason", "—"), sb.get("reason", "—")
        na, nb_ = sa.get("n_pass", 0), sb.get("n_pass", 0)
        delta = nb_ - na
        sign = f"+{delta}" if delta > 0 else str(delta)
        title = iid[:22]
        lines.append(f"{title:<22} {ra:<7} {rb:<7} {sign}")
    la = a.get("label") or f"cycle_{a.get('cycle')}"
    lb = b.get("label") or f"cycle_{b.get('cycle')}"
    return f"{la} → {lb}\n" + "\n".join(lines) + "\n"


def format_item_table(item_scores: list[dict]) -> str:
    lines = ["item                    score   fail"]
    for spec in item_scores:
        fails = ",".join(h["id"] for h in spec.get("rows") or [] if not h.get("pass")) or "—"
        lines.append(f"{spec['id']:<22} {spec.get('reason', '0/0'):<7} {fails}")
    return "\n".join(lines) + "\n"


def mode_acceptable(kind: str, rows: list[dict]) -> tuple[bool, str]:
    scored = [r for r in rows if not r.get("skipped")]
    skipped = [r for r in rows if r.get("skipped")]
    n_pass = sum(1 for r in scored if r.get("pass"))
    n = len(scored)
    trap_lie = any(r.get("trap_lie") for r in rows)
    if kind == "cubre" and trap_lie:
        return False, "cubre_trampa_miente"
    if n == 0:
        return False, "sin_tiradas"
    if n_pass == n:
        return True, f"{n_pass}/{n}"
    if n_pass == n - 1 and skipped:
        return True, f"{n_pass}/{n}+thin_skip"
    fail_only_empty_graph = all(
        r.get("pass") or "thin_payload_not_empty" in (r.get("notes") or []) for r in rows
    )
    if fail_only_empty_graph and n_pass >= n - 1:
        return True, "2/3_thin_skip"
    return False, f"{n_pass}/{n}"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", default="all", choices=("cubre", "como", "gap", "all"))
    ap.add_argument("--pack", default="tune", choices=("tune", "robustness", "items", "synth", "tree"))
    ap.add_argument("--cycle", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=600)
    ap.add_argument("--cards-root", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--from", dest="from_dir", default="", help="Rescore artefactos de un ciclo previo (sin LLM)")
    ap.add_argument("--compare", default="", help="Compara item_scores de dos ciclos, p.ej. 0,1")
    ap.add_argument("--label", default="", help="Etiqueta del ciclo (p.ej. el ítem de producto parcheado)")
    args = ap.parse_args()
    if args.compare:
        parts = [x.strip() for x in args.compare.split(",") if x.strip()]
        if len(parts) != 2:
            raise SystemExit("--compare espera A,B (números de ciclo)")
        paths = [TUNE_ROOT / "items" / f"cycle_{p}" / "score.json" for p in parts]
        missing = [str(p) for p in paths if not p.exists()]
        if missing:
            raise SystemExit("faltan scores: " + ", ".join(missing))
        a = json.loads(paths[0].read_text(encoding="utf-8"))
        b = json.loads(paths[1].read_text(encoding="utf-8"))
        print(compare_item_scores(a, b), end="")
        return 0
    pack = load_pack(args.pack)
    cards_root = Path(args.cards_root or pack.get("cards_root") or "")
    if not cards_root.is_absolute():
        cards_root = ROOT / cards_root
    workspace = Path(pack.get("workspace") or "")
    if str(workspace) and not workspace.is_absolute():
        workspace = ROOT / workspace
    if not str(pack.get("workspace") or ""):
        workspace = None
    cases = list(pack["cases"])
    if args.kind != "all":
        cases = [c for c in cases if c.get("kind") == args.kind]
    if args.only:
        wanted = {x.strip() for x in args.only.split(",") if x.strip()}
        cases = [c for c in cases if c["id"] in wanted]
    if not cases:
        raise SystemExit(f"no cases for kind={args.kind} pack={args.pack}")
    from_dir = Path(args.from_dir) if args.from_dir else None
    if from_dir and not from_dir.is_absolute():
        from_dir = ROOT / from_dir
    if not from_dir and not CLI.exists():
        raise SystemExit(f"missing {CLI}; cmake --build build --target l2_harness_cli")
    pack_name = str(pack.get("pack") or args.pack)
    if pack_name == "robustness":
        cycle_dir = TUNE_ROOT / "robustness" / f"cycle_{args.cycle}"
        state_path = TUNE_ROOT / "robustness" / "state.json"
        score_kind = "robustness"
    elif pack_name == "items":
        cycle_dir = TUNE_ROOT / "items" / f"cycle_{args.cycle}"
        state_path = TUNE_ROOT / "items" / "state.json"
        score_kind = "items"
    elif pack_name == "synth":
        cycle_dir = TUNE_ROOT / "synth" / f"cycle_{args.cycle}"
        state_path = TUNE_ROOT / "synth" / "state.json"
        score_kind = "synth"
    elif pack_name == "tree":
        cycle_dir = TUNE_ROOT / "tree" / f"cycle_{args.cycle}"
        state_path = TUNE_ROOT / "tree" / "state.json"
        score_kind = "tree"
    else:
        cycle_dir = TUNE_ROOT / args.kind / f"cycle_{args.cycle}"
        state_path = TUNE_ROOT / "state.json"
        score_kind = args.kind
    cycle_dir.mkdir(parents=True, exist_ok=True)
    env = env_base()
    rows = []
    traces = []
    for case in cases:
        if from_dir:
            print(f"==== rescore {case.get('kind')} {case['id']} ====", flush=True)
            row = rescore_from(case, from_dir, cards_root)
        else:
            out_dir = cycle_dir / case["id"]
            print(f"==== probe {case.get('kind')} {case['id']} ====", flush=True)
            row = run_one(case, cards_root, out_dir, env, args.timeout, workspace)
        rows.append(row)
        brief = str(row.get("brief") or "")
        traces.append(
            f"## {case['id']} family={case.get('family')} pass={row.get('pass')} "
            f"verdict={row.get('verdict')} raw={row.get('raw_verdict')}\n"
            f"notes={row.get('notes')}\n"
            f"brief={brief}\n"
            + "\n".join(
                f"- {t.get('tag')} {t.get('tool')} {t.get('target')}"
                for t in (row.get("tools") or [])
            )
            + "\n"
        )
        print(
            f"  pass={row.get('pass')} verdict={row.get('verdict')} "
            f"brief_ok={row.get('brief_ok')} tools={len(row.get('tools') or [])} "
            f"notes={row.get('notes')}",
            flush=True,
        )
    item_scores = score_items(pack.get("item_defs") or [], rows) if score_kind == "items" else []
    ok, reason = mode_acceptable(score_kind if score_kind not in ("robustness", "items") else "all", rows)
    if score_kind == "robustness":
        scored = [r for r in rows if not r.get("skipped")]
        n_pass = sum(1 for r in scored if r.get("pass"))
        ok = not any(r.get("trap_lie") for r in rows)
        reason = f"{n_pass}/{len(scored)}"
        if any(r.get("trap_lie") for r in rows):
            reason = "cubre_trampa_miente+" + reason
    if score_kind == "items":
        scored = [r for r in rows if not r.get("skipped")]
        n_pass = sum(1 for r in scored if r.get("pass"))
        ok = True
        reason = f"{n_pass}/{len(scored)}"
        if item_scores:
            reason = " ".join(f"{s['id']}={s['reason']}" for s in item_scores)
    summary = {
        "kind": score_kind,
        "cycle": args.cycle,
        "label": args.label or (f"from:{from_dir}" if from_dir else ""),
        "from": str(from_dir) if from_dir else "",
        "acceptable": ok,
        "reason": reason,
        "trap_lie": any(r.get("trap_lie") for r in rows),
        "n_pass": sum(1 for r in rows if r.get("pass") and not r.get("skipped")),
        "n": sum(1 for r in rows if not r.get("skipped")),
        "n_brief_ok": sum(1 for r in rows if r.get("brief_ok")),
        "item_scores": item_scores,
        "rows": rows,
    }
    (cycle_dir / "score.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    trace_body = "\n".join(traces)
    if item_scores:
        trace_body = "# ítems\n\n" + format_item_table(item_scores) + "\n" + trace_body
    (cycle_dir / "trace.md").write_text(trace_body, encoding="utf-8")
    state = {
        "mode": score_kind,
        "cycle": args.cycle,
        "label": summary["label"],
        "last_score": reason,
        "acceptable": ok,
        "trap_lie": summary["trap_lie"],
        "stop_reason": "",
    }
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    print("==== SCORE ====", flush=True)
    printable = {k: summary[k] for k in summary if k not in ("rows", "item_scores")}
    print(json.dumps(printable, indent=2), flush=True)
    if item_scores:
        print("==== ITEMS ====", flush=True)
        print(format_item_table(item_scores), end="", flush=True)
    return 0 if score_kind in ("robustness", "items") else (0 if ok else 1)


if __name__ == "__main__":
    raise SystemExit(main())
