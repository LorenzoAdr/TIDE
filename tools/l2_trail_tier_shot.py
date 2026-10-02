#!/usr/bin/env python3
"""One-shot: 7B labels caller-supplied trail ids as must/should/low/reject."""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
LLAMA = Path.home() / ".cache/tuide/models/runtime/llama-b10333/llama-server"
MODEL = Path.home() / ".cache/tuide/models/l2/qwen2.5-coder-7b-instruct-q4_k_m.gguf"
PORT = 18766

SYSTEM = """Eres el Nivel 2. JSON only. PROHIBIDO markdown/prosa fuera del JSON.
Tarea: etiquetar CADA id indicado para el síntoma de Instruction.
Tiers (peso de pack, NO 'el único edit site'):
- must: hay que abrir el cuerpo (caller/control del síntoma).
- should: relevante; cuerpo si cabe.
- low: relacionado débil; basta firma.
- reject: ruido / otro feature (reindex, outline, tests).
Reglas: exactamente esos ids, cada uno una vez. must 0–2. should 0–3. El resto low|reject.
{"action":"a_tier","must":["S1"],"should":["L0"],"low":[],"reject":["S2","S3"]}
"""


def instruction_for(case_id: str) -> str:
    arr = json.loads(PROMPTS.read_text())
    for c in arr:
        if c.get("id") == case_id:
            return c.get("prompt") or ""
    raise SystemExit(f"case not found: {case_id}")


def strip_old_schema(trail: str) -> str:
    cut = trail.find("Responde con verdict")
    return trail[:cut].rstrip() if cut >= 0 else trail.rstrip()


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
    for _ in range(60):
        if port_open(PORT):
            return proc
        time.sleep(0.5)
    proc.terminate()
    raise SystemExit("llama-server no arrancó en :18766")


def chat(system: str, user: str, max_tokens: int = 256, temperature: float = 0.1) -> str:
    budget = 64  # Low: a_trail_judge / micro-JSON
    payload = {
        "model": "l2",
        "temperature": temperature,
        "max_tokens": max_tokens + budget,
        "chat_template_kwargs": {"enable_thinking": True},
        "thinking_budget_tokens": budget,
        "reasoning_budget_tokens": budget,
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


def normalize_tier(obj: dict, expected_ids: list[str]) -> dict:
    out = {k: [] for k in ("must", "should", "low", "reject")}
    if not obj:
        return out
    for k in out:
        vals = obj.get(k) or []
        if isinstance(vals, str):
            vals = [vals]
        for v in vals:
            v = str(v).strip()
            if v in expected_ids and v not in sum(out.values(), []):
                out[k].append(v)
    seen = set(sum(out.values(), []))
    for i in expected_ids:
        if i not in seen:
            out["low"].append(i)  # missing → low (runtime fill)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--trail", type=Path, required=True)
    ap.add_argument("--case", required=True)
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--out", type=Path, default=None)
    args = ap.parse_args()
    trail_path = args.trail
    if not trail_path.is_file():
        raise SystemExit(f"no trail: {trail_path}")
    out_dir = args.out or (
        ROOT / ".tuide/ai/l2_explore_battery/round_trail_tier"
    )
    out_dir.mkdir(parents=True, exist_ok=True)

    instr = instruction_for(args.case)
    trail_body = strip_old_schema(trail_path.read_text(encoding="utf-8", errors="replace"))
    expected_ids = []
    if re.search(r"\bL0\b", trail_body):
        expected_ids.append("L0")
    expected_ids.extend(
        sorted(set(re.findall(r"\bS\d+\b", trail_body)), key=lambda x: int(x[1:]))
    )
    if not expected_ids:
        raise SystemExit("trail sin ids L0/S*")
    user = (
        f"phase=explore_a subfase=a1_trail\n\n## Instruction\n{instr}\n\n"
        f"## Trail\n{trail_body}\n\n"
        "## Ids a etiquetar (copia literales)\n"
        + ", ".join(f"`{item}`" for item in expected_ids)
        + "\nEmite a_tier cubriendo exactamente esos ids.\n"
    )
    (out_dir / "system.txt").write_text(SYSTEM)
    (out_dir / "user.md").write_text(user)

    spawned = ensure_server()
    rows = []
    try:
        for i in range(args.repeats):
            raw = chat(SYSTEM, user)
            obj = extract_json(raw)
            tiers = normalize_tier(obj, expected_ids)
            rows.append({"repeat": i + 1, "raw": raw, "parsed": obj, "tiers": tiers})
            (out_dir / f"raw_{i + 1}.txt").write_text(raw)
            print(f"==== repeat {i + 1} ====")
            print(raw.strip()[:800])
            print("normalized:", json.dumps(tiers, ensure_ascii=False))
            print()
    finally:
        if spawned is not None:
            spawned.terminate()

    (out_dir / "results.json").write_text(json.dumps(rows, indent=2, ensure_ascii=False))
    # stability
    print("==== counts per id ====")
    for eid in expected_ids:
        bags = [r["tiers"] for r in rows]
        labels = []
        for b in bags:
            for k, vs in b.items():
                if eid in vs:
                    labels.append(k)
        print(f"  {eid}: {labels}")
    print(f"artifacts: {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
