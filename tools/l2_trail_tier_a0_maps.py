#!/usr/bin/env python3
"""Probe: 7B a_tier on caller-supplied A0 expansion targets."""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
BIN = ROOT / "build/l2_harness_cli"
LLAMA = Path.home() / ".cache/tuide/models/runtime/llama-b10333/llama-server"
MODEL = Path.home() / ".cache/tuide/models/l2/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
PORT = 18766
SYSTEM = """Eres el Nivel 2. JSON only. PROHIBIDO markdown/prosa fuera del JSON.
Etiqueta CADA id de la lista para el síntoma de Instruction.
Tiers (peso de pack, NO 'el único edit site'):
- must: caller/control que explica el síntoma; hay que abrir el cuerpo.
- should: relevante; cuerpo si cabe.
- low: relacionado débil; basta firma.
- reject: otro feature, tests, ruido. Si el L0 entero es otro feature, reject L0 y sus S*.
Reglas: exactamente los ids listados, cada uno una vez. must 0–2. should 0–3. El resto low|reject.
{"action":"a_tier","must":[],"should":[],"low":[],"reject":[]}
"""

ID_RE = re.compile(r"`(L0|S\d+|ON|CXL|OFF|LINK)`")


def instruction_for(case_id: str) -> str:
    for c in json.loads(PROMPTS.read_text()):
        if c.get("id") == case_id:
            return c.get("prompt") or ""
    raise SystemExit(f"case not found: {case_id}")


def strip_hash(target: str) -> str:
    return target.split("#", 1)[0].strip()


def parse_target(target: str) -> tuple[str, str]:
    t = strip_hash(target)
    if ":" not in t:
        return "", t
    path, sym = t.rsplit(":", 1)
    return path, sym


def port_open(port: int) -> bool:
    s = socket.socket()
    s.settimeout(0.4)
    try:
        s.connect(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def ensure_server() -> subprocess.Popen | None:
    if port_open(PORT):
        return None
    if not LLAMA.is_file() or not MODEL.is_file():
        raise SystemExit(f"missing llama or model: {LLAMA} {MODEL}")
    proc = subprocess.Popen(
        [
            str(LLAMA),
            "-m",
            str(MODEL),
            "--host",
            "127.0.0.1",
            "--port",
            str(PORT),
            "-c",
            "8192",
            "-ngl",
            "0",
            "-t",
            "12",
            "-np",
            "1",
            "--log-disable",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    for _ in range(80):
        if port_open(PORT):
            return proc
        time.sleep(0.5)
    proc.terminate()
    raise SystemExit("llama-server no arrancó en :18766")


def chat(system: str, user: str, max_tokens: int = 256, temperature: float = 0.1) -> str:
    payload = {
        "model": "l2",
        "temperature": temperature,
        "max_tokens": max_tokens,
        "messages": [
            {"role": "system", "content": system},
            {"role": "user", "content": user},
        ],
    }
    req = urllib.request.Request(
        f"http://127.0.0.1:{PORT}/v1/chat/completions",
        data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=180) as resp:
        body = json.loads(resp.read().decode())
    return body["choices"][0]["message"]["content"]


def extract_json(text: str) -> dict | None:
    text = text.strip()
    fence = re.search(r"```(?:json)?\s*(\{.*?\})\s*```", text, re.S)
    raw = fence.group(1) if fence else text
    start = raw.find("{")
    end = raw.rfind("}")
    if start < 0 or end <= start:
        return None
    try:
        return json.loads(raw[start : end + 1])
    except json.JSONDecodeError:
        return None


def normalize_tier(obj: dict | None, expected_ids: list[str]) -> dict:
    out = {k: [] for k in ("must", "should", "low", "reject")}
    if not obj:
        return out
    seen: set[str] = set()
    for k in out:
        vals = obj.get(k) or []
        if isinstance(vals, str):
            vals = [vals]
        for v in vals:
            v = str(v).strip()
            if v in expected_ids and v not in seen:
                out[k].append(v)
                seen.add(v)
    for i in expected_ids:
        if i not in seen:
            out["low"].append(i)
    return out


def trail_probe(path: str, sym: str) -> str:
    if not BIN.is_file():
        raise SystemExit(f"missing {BIN}")
    cmd = [str(BIN), "trail-probe", sym]
    if path:
        cmd += ["--path", path]
    p = subprocess.run(
        cmd,
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        env={**os.environ, "TUIDE_ROOT": str(ROOT)},
        timeout=60,
    )
    return (p.stdout or "") + (("\n" + p.stderr) if p.stderr else "")


def ids_from_trail(md: str) -> list[str]:
    found: list[str] = []
    for m in ID_RE.finditer(md):
        i = m.group(1)
        if i not in found:
            found.append(i)
    if "L0" not in found:
        found.insert(0, "L0")
    return found


def slug(target: str) -> str:
    return re.sub(r"[^a-zA-Z0-9]+", "_", target).strip("_")[:80]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--case", required=True)
    ap.add_argument("--target", action="append", required=True)
    ap.add_argument(
        "--out",
        type=Path,
        default=ROOT / ".tuide/ai/l2_explore_battery/round_trail_tier_a0_maps",
    )
    args = ap.parse_args()
    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)
    instr = instruction_for(args.case)
    targets = [{"target": strip_hash(target), "sources": ["cli"]} for target in args.target]
    (out_dir / "targets.json").write_text(json.dumps(targets, indent=2))
    (out_dir / "system.txt").write_text(SYSTEM)
    print(f"A0 expand L0s: {len(targets)}")
    for t in targets:
        print(f"  - {t['target']}  src={','.join(t['sources'])}")

    spawned = ensure_server()
    rows = []
    try:
        for t in targets:
            path, sym = parse_target(t["target"])
            raw_probe = trail_probe(path, sym)
            cut = raw_probe.find("## Trail")
            trail_md = raw_probe[cut:].strip() if cut >= 0 else raw_probe.strip()
            trail_md = trail_md.split("Responde con verdict")[0].rstrip()
            ids = ids_from_trail(trail_md)
            d = out_dir / slug(t["target"])
            d.mkdir(parents=True, exist_ok=True)
            (d / "trail.md").write_text(trail_md)
            id_lines = "\n".join(f"- `{i}`" for i in ids)
            user = (
                f"phase=explore_a subfase=a1_trail\n\n## Instruction\n{instr}\n\n"
                f"## Trail\n{trail_md}\n\n"
                f"## Ids a etiquetar (copia literales, todos)\n{id_lines}\n"
                "Emite a_tier cubriendo EXACTAMENTE esos ids.\n"
            )
            (d / "user.md").write_text(user)
            print(f"\n==== {t['target']} ids={ids} ====")
            raw = chat(SYSTEM, user)
            (d / "raw.txt").write_text(raw)
            obj = extract_json(raw)
            tiers = normalize_tier(obj, ids)
            (d / "tiers.json").write_text(json.dumps(tiers, indent=2))
            must = tiers["must"]
            l0_tier = next((k for k, vs in tiers.items() if "L0" in vs), "missing")
            rec = {
                "target": t["target"],
                "sources": t["sources"],
                "ids": ids,
                "raw": raw,
                "parsed": obj,
                "tiers": tiers,
                "l0_tier": l0_tier,
                "must": must,
            }
            rows.append(rec)
            print(raw.strip()[:500])
            print(
                f"tiers={json.dumps(tiers, ensure_ascii=False)} l0={l0_tier}"
            )
    finally:
        if spawned is not None:
            spawned.terminate()

    (out_dir / "results.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False))
    print("\n==== resumen ====")
    must_n = [len(r["must"]) for r in rows]
    print(f"must counts: {must_n}  mean={sum(must_n)/max(len(must_n),1):.2f}")
    for r in rows:
        print(
            f"  L0={r['l0_tier']:7} must={r['must']}  "
            f"{r['target']}"
        )
    print(f"artifacts: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
