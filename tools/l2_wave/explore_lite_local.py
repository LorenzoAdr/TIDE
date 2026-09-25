#!/usr/bin/env python3
"""H_LITE_T2: sense core5 with Grep+Read only, driven by local OpenAI-compatible LLM.

Same protocol as H_LITE / Cursor lite battery, but the model is the remote L2
(api_base), not Cursor. Writes scorable control.json per case.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_API = "http://192.168.64.1:8080/v1"
DEFAULT_MODEL = "Qwen3-Coder-Next-80B-A3B-Q4_K_M"
MANIFEST = ROOT / "tests/fixtures/l2_wave/sense_battery.json"
PROMPTS = ROOT / "tests/fixtures/stem_boost_battery/prompts_nl_human.json"
SCORE = ROOT / "tools/l2_wave/score_sense_battery.py"

PILOT_SYS = """Eres el PILOTO de una caza en un repo C++ (TIDE). NO lees código tú.
Solo decides: explorar (hijo con consulta estrecha) o cerrar el claim.
Máximo 3 jobs de explorar. consulta = fenómeno en lenguaje natural (sin paths, sin M*, sin stems con _).
Responde UN JSON:
{"do":"explorar","consulta":"...","why":"..."}
{"do":"cerrar","why":"...","veredicto":"hay|no_hay|no_concluyente","simbolos":[]}
Tras un no_hay del hijo, parte polos (otra consulta); no selles el ancla al primer miss.
Si la evidencia basta: cerrar con why honesto (confirmar, refutar o rendirse)."""

def explorer_system(tools: list[str]) -> str:
    allowed = "/".join(tools + ["cerrar"])
    lines = [
        f"Eres el EXPLORADOR. Tools permitidas: {allowed}. Responde UN JSON por turno:",
    ]
    if "grep" in tools:
        lines.append(
            '{"do":"grep","pattern":"...","glob":"src/**/*.{cpp,hpp}","why":"..."}'
        )
    if "read" in tools:
        lines.append(
            '{"do":"read","path":"src/...","offset":1,"limit":120,"why":"..."}'
        )
    if "peek" in tools:
        lines.append(
            '{"do":"peek","path":"src/...","offset":1,"why":"..."}'
        )
        lines.append("peek: lectura CORTA (~40 líneas). Preferir read si necesitas cuerpo.")
    if "needles" in tools:
        lines.append(
            '{"do":"needles","needles":["symbol_a","symbol_b"],"why":"..."}'
        )
        lines.append("needles: 1–6 identificadores; busca palabra acotada en src/.")
    if "cerca" in tools:
        lines.append(
            '{"do":"cerca","needles":["concepto corto","otro"],"why":"..."}'
        )
        lines.append(
            "cerca: MÁXIMO 1 por job. Rank semántico → luego read de READABLE_PATHS (no inventes paths)."
        )
    if "atlas" in tools:
        lines.append(
            '{"do":"atlas","query":"frase de la consulta","why":"..."}'
        )
        lines.append("atlas: hypotéticos M*/zonas; no son verdad — solo pista.")
    if "inbody" in tools:
        lines.append(
            '{"do":"inbody","path":"src/x.cpp","pattern":"needle","why":"..."}'
        )
        lines.append("inbody: grep dentro de UN archivo; 0 hits es evidencia.")
    if "follow" in tools:
        lines.append(
            '{"do":"follow","symbol":"Class::method_or_fn","path":"src/optional.cpp","why":"..."}'
        )
        lines.append(
            "follow: callers (rg) + callees heurísticos del path si lo das. No sustituye a read."
        )
    if "entre" in tools:
        lines.append(
            '{"do":"entre","from":"symbol_a","to":"symbol_b","why":"..."}'
        )
        lines.append(
            "entre: ¿hay co-ocurrencia/camino débil entre dos símbolos? sin camino también es evidencia."
        )
    lines.append(
        '{"do":"cerrar","veredicto":"encontrado|no_encontrado|parcial","simbolos":["path:symbol"],'
        '"evidencia":["path:línea o cita corta"],'
        '"falta":["parte de TU consulta no confirmada; [] solo si lo cubriste todo"],'
        '"why":"..."}'
    )
    lines.append(
        "pattern = regex ripgrep. read: path relativo; offset/limit de líneas. "
        "Responde SOLO a TU consulta (el brief del piloto), no al claim final del usuario. "
        "cerrar.veredicto = si hallaste el objeto de ESA consulta: encontrado / no_encontrado / parcial. "
        "why = hechos y citas; NO digas que la feature del usuario existe o que hay que implementarla. "
        "falta (alias no_visto) = OBLIGATORIO al cerrar: lista de partes de TU consulta no confirmadas "
        "(vacía [] solo si lo cubriste todo). Si niegas el objeto de TU consulta (no está / no hay / "
        "no se halló), NO uses veredicto=encontrado: usa no_encontrado o parcial y ponlo en falta. "
        "Si lo leído es otra cosa o solo un vecino: parcial o no_encontrado, y dilo en falta. "
        "Cuando ya puedas citar path:symbol (o ausencia clara), cierra; no agotes olas por costumbre. "
        "PROHIBIDO inventar paths no devueltos por grep/cerca. "
        "Máximo ~10 olas totales; ~6 greps y ~6 reads; luego cierra."
    )
    return "\n".join(lines)


def chat(api: str, model: str, messages: list[dict], max_tokens: int = 700, temperature: float = 0.1) -> str:
    url = api.rstrip("/") + "/chat/completions"
    # Qwen3.x thinking models often put the answer only after reasoning; with empty
    # content the harness starves. Disable thinking when the server supports it.
    body = json.dumps(
        {
            "model": model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": temperature,
            "chat_template_kwargs": {"enable_thinking": False},
            "enable_thinking": False,
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        url, data=body, headers={"Content-Type": "application/json"}, method="POST"
    )
    with urllib.request.urlopen(req, timeout=600) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    msg = data["choices"][0]["message"]
    content = msg.get("content") or ""
    # some models put reasoning separately
    if isinstance(content, list):
        content = "".join(
            p.get("text", "") if isinstance(p, dict) else str(p) for p in content
        )
    content = str(content).strip()
    if not content:
        # last resort: scrape JSON from reasoning_content
        reason = str(msg.get("reasoning_content") or "").strip()
        if reason:
            m = re.search(r"\{[\s\S]*\}", reason)
            if m:
                return m.group(0).strip()
            return reason
    return content


def extract_json(text: str) -> dict:
    text = text.strip()
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text)
        text = re.sub(r"\s*```$", "", text)
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass
    m = re.search(r"\{[\s\S]*\}", text)
    if not m:
        raise ValueError(f"no JSON in: {text[:400]}")
    return json.loads(m.group(0))


def tool_grep(pattern: str, glob: str | None, root: Path) -> str:
    """Search under src/ (or .). Use --json: Cursor's rg drops paths with --glob."""
    search_path = "src"
    suffix_allow: set[str] | None = {".cpp", ".hpp", ".h", ".cc", ".cxx"}
    if glob:
        g = str(glob)
        if g.startswith("tests/") or g.startswith("**") or g.startswith("."):
            search_path = "."
            suffix_allow = None
        # extract extensions from brace if present
        m = re.search(r"\.\{([^}]+)\}", g)
        if m:
            suffix_allow = {"." + a.strip().lstrip(".") for a in m.group(1).split(",") if a.strip()}
        elif "*." in g:
            # e.g. *.cpp alone — keep default suffixes unless clear
            pass
    cmd = [
        "rg",
        "--json",
        "-S",
        "-I",
        "--max-filesize",
        "1M",
        "-m",
        "40",
        pattern,
        search_path,
    ]
    proc = subprocess.run(
        cmd,
        cwd=root,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    lines_out: list[str] = []
    paths_ordered: list[str] = []
    seen_p: set[str] = set()
    for raw_ln in (proc.stdout or "").splitlines():
        try:
            ev = json.loads(raw_ln)
        except json.JSONDecodeError:
            continue
        if ev.get("type") != "match":
            continue
        data = ev.get("data") or {}
        path = ((data.get("path") or {}).get("text")) or ""
        if not path:
            continue
        if suffix_allow is not None and Path(path).suffix not in suffix_allow:
            continue
        ln = data.get("line_number")
        text = ((data.get("lines") or {}).get("text") or "").rstrip("\n")
        lines_out.append(f"{path}:{ln}:{text}")
        if path not in seen_p:
            seen_p.add(path)
            paths_ordered.append(path)
        if len(lines_out) >= 40:
            break
    if not lines_out:
        err = (proc.stderr or "").strip()[:500]
        return f"(0 hits){(' stderr: ' + err) if err else ''}"
    return "\n".join(lines_out)


def paths_from_rg(text: str) -> list[str]:
    """Extract repo-relative paths from rg -H lines (path:line:...)."""
    found: list[str] = []
    seen: set[str] = set()
    for ln in (text or "").splitlines():
        if ln.startswith("(") or ln.startswith("..."):
            continue
        # path may contain colons on Windows; for our repo paths are src/...
        m = re.match(r"^((?:src|tests|tools|docs|cmake)/[^:]+):\d+", ln)
        if not m:
            continue
        p = m.group(1)
        if p not in seen:
            seen.add(p)
            found.append(p)
    return found


def resolve_read_path(path: str, root: Path, allowed: set[str]) -> tuple[str, str | None]:
    """Return (resolved_relpath, error). Prefer exact allowed; else unique basename under src/."""
    raw = (path or "").strip().strip("`")
    if not raw:
        return "", "error: read requiere path"
    p = Path(raw)
    if p.is_absolute():
        try:
            raw = str(p.relative_to(root))
        except ValueError:
            return raw, f"error: path fuera del repo: {path}"
    raw = raw.lstrip("./")
    if raw in allowed:
        return raw, None
    # basename unique match among allowed
    base = Path(raw).name
    hits = [a for a in allowed if Path(a).name == base]
    if len(hits) == 1:
        return hits[0], None
    if allowed:
        sample = ", ".join(sorted(allowed)[:8])
        return raw, (
            f"error: path no visto en hits previos: `{raw}`. "
            f"Usa uno de: {sample}"
            + (" …" if len(allowed) > 8 else "")
        )
    # no hits yet: allow if file exists (first move) — still prefer honesty
    full = (root / raw).resolve()
    if str(full).startswith(str(root.resolve())) and full.is_file():
        return raw, None
    # fuzzy unique basename under src/
    cands = list((root / "src").rglob(base)) if (root / "src").is_dir() else []
    cands = [c for c in cands if "._" not in c.name]
    if len(cands) == 1:
        return str(cands[0].relative_to(root)), None
    return raw, f"error: no existe o path no anclado: {raw}"


def tool_read(path: str, offset: int, limit: int, root: Path, allowed: set[str] | None = None) -> str:
    if allowed is not None:
        resolved, err = resolve_read_path(path, root, allowed)
        if err:
            return err
        path = resolved
    p = Path(path)
    if p.is_absolute():
        try:
            p = p.relative_to(root)
        except ValueError:
            return f"error: path fuera del repo: {path}"
    full = (root / p).resolve()
    if not str(full).startswith(str(root.resolve())):
        return "error: path escape"
    if not full.is_file():
        return f"error: no existe {p}"
    raw = full.read_text(encoding="utf-8", errors="replace").splitlines()
    off = max(1, int(offset or 1))
    lim = max(1, min(int(limit or 120), 250))
    chunk = raw[off - 1 : off - 1 + lim]
    numbered = [f"{i}|{line}" for i, line in enumerate(chunk, start=off)]
    return f"# {p} lines {off}-{off + len(chunk) - 1} / {len(raw)}\n" + "\n".join(numbered)


def tool_follow(symbol: str, path: str | None, root: Path) -> str:
    """Cheap stand-in for wave follow: callers via rg + callees from a file slice."""
    sym = (symbol or "").strip()
    if len(sym) < 2:
        return "error: follow requiere symbol"
    short = sym.split("::")[-1]
    callers = tool_grep(re.escape(short), "src/**/*.{cpp,hpp,h}", root)
    parts = [f"## callers (~ `{short}`)\n```\n{callers[:3500]}\n```"]
    if path:
        body = tool_read(path, 1, 200, root)
        # heuristic callees: Foo( or foo(
        calls = sorted(
            set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]{2,})\s*\(", body))
        )[:40]
        parts.append(
            f"## callees heurísticos en `{path}` (primeras ~200 líneas)\n"
            + (", ".join(calls) if calls else "(ninguno)")
        )
        parts.append(f"## slice\n```\n{body[:2500]}\n```")
    else:
        parts.append("(sin path: solo callers; pasa path para callees)")
    return "\n\n".join(parts)


def tool_entre(frm: str, to: str, root: Path) -> str:
    """Weak path/co-occurrence check between two symbols (hueco-friendly)."""
    a = (frm or "").strip()
    b = (to or "").strip()
    if len(a) < 2 or len(b) < 2:
        return "error: entre requiere from y to"
    sa, sb = a.split("::")[-1], b.split("::")[-1]
    ha = tool_grep(re.escape(sa), "src/**/*.{cpp,hpp,h}", root)
    hb = tool_grep(re.escape(sb), "src/**/*.{cpp,hpp,h}", root)
    files_a = {ln.split(":", 1)[0] for ln in ha.splitlines() if ":" in ln and not ln.startswith("(")}
    files_b = {ln.split(":", 1)[0] for ln in hb.splitlines() if ":" in ln and not ln.startswith("(")}
    both = sorted(files_a & files_b)
    if both:
        return (
            f"co-ocurrencia: ambos aparecen en {len(both)} archivo(s): "
            + ", ".join(both[:12])
            + (" …" if len(both) > 12 else "")
            + f"\n\n## hits `{sa}` (muestra)\n```\n{ha[:2000]}\n```\n"
            + f"## hits `{sb}` (muestra)\n```\n{hb[:2000]}\n```"
        )
    if ha.startswith("(0 hits") and hb.startswith("(0 hits"):
        return f"sin camino: 0 hits para `{sa}` y `{sb}`"
    if ha.startswith("(0 hits"):
        return f"sin camino: `{sa}` no aparece; `{sb}` sí\n```\n{hb[:2500]}\n```"
    if hb.startswith("(0 hits"):
        return f"sin camino: `{sb}` no aparece; `{sa}` sí\n```\n{ha[:2500]}\n```"
    return (
        f"sin co-ocurrencia de archivo entre `{sa}` y `{sb}` "
        f"(archivos distintos; no prueba un call-path fuerte)\n"
        f"A={len(files_a)} files B={len(files_b)} files"
    )


def tool_peek(path: str, offset: int, root: Path) -> str:
    """Wave-like short peek (intentionally truncated)."""
    return tool_read(path, offset or 1, 40, root)


def tool_needles(needles: list, root: Path) -> str:
    ns = [str(n).strip() for n in (needles or []) if str(n).strip()]
    if not ns or len(ns) > 6:
        return "error: needles 1–6 identificadores"
    parts = []
    for n in ns:
        # word-ish boundary for C++ identifiers
        pat = r"\b" + re.escape(n.split("::")[-1]) + r"\b"
        parts.append(f"## needle `{n}`\n```\n{tool_grep(pat, 'src/**/*.{cpp,hpp,h}', root)[:2500]}\n```")
    return "\n\n".join(parts)


def tool_inbody(path: str, pattern: str, root: Path) -> str:
    p = Path(path)
    if p.is_absolute():
        try:
            p = p.relative_to(root)
        except ValueError:
            return f"error: path fuera del repo: {path}"
    full = (root / p).resolve()
    if not str(full).startswith(str(root.resolve())) or not full.is_file():
        return f"error: no legible {path}"
    text = full.read_text(encoding="utf-8", errors="replace")
    pat = pattern or ""
    if not pat:
        return "error: inbody requiere pattern"
    hits = []
    try:
        cre = re.compile(pat)
    except re.error as e:
        return f"error: regex {e}"
    for i, line in enumerate(text.splitlines(), 1):
        if cre.search(line):
            hits.append(f"{i}:{line[:200]}")
            if len(hits) >= 30:
                break
    if not hits:
        return f"inbody `{p}` pattern=`{pat}`: 0 hits (ausencia local)"
    return f"inbody `{p}` pattern=`{pat}` hits={len(hits)}\n" + "\n".join(hits)


_EMBED_CACHE: dict[str, list[float]] | None = None
_EMBED_API = "http://192.168.64.1:18765/v1"


def _embed(texts: list[str]) -> list[list[float]]:
    body = json.dumps({"input": texts, "model": "nomic-embed-text-v1.5"}).encode("utf-8")
    req = urllib.request.Request(
        _EMBED_API.rstrip("/") + "/embeddings",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    rows = sorted(data.get("data") or [], key=lambda r: r.get("index", 0))
    return [r["embedding"] for r in rows]


def _cos(a: list[float], b: list[float]) -> float:
    import math

    dot = sum(x * y for x, y in zip(a, b))
    na = math.sqrt(sum(x * x for x in a)) or 1e-9
    nb = math.sqrt(sum(x * x for x in b)) or 1e-9
    return dot / (na * nb)


def _passage_index(root: Path) -> list[tuple[str, str]]:
    """(id, text) for semantic rank — file paths under src/."""
    out: list[tuple[str, str]] = []
    src = root / "src"
    if not src.is_dir():
        return out
    for p in sorted(src.rglob("*")):
        if p.suffix not in {".cpp", ".hpp", ".h", ".cc"}:
            continue
        if "._" in p.name:
            continue
        rel = str(p.relative_to(root))
        stem = p.stem.replace("_", " ")
        parent = p.parent.name.replace("_", " ")
        out.append((rel, f"{parent} {stem} {rel}"))
        if len(out) >= 220:
            break
    return out


def _ensure_embed_cache(root: Path) -> dict[str, list[float]]:
    global _EMBED_CACHE
    cache_path = root / ".tuide/ai/l2_wave/v2_night/TOOL_EVAL/_embed_paths.json"
    if _EMBED_CACHE is not None:
        return _EMBED_CACHE
    if cache_path.is_file():
        raw = json.loads(cache_path.read_text(encoding="utf-8"))
        _EMBED_CACHE = {k: v for k, v in raw.items()}
        return _EMBED_CACHE
    passages = _passage_index(root)
    _EMBED_CACHE = {}
    # batch embed
    batch: list[tuple[str, str]] = []
    for pid, text in passages:
        batch.append((pid, text))
        if len(batch) >= 32:
            vecs = _embed([t for _, t in batch])
            for (pid2, _), v in zip(batch, vecs):
                _EMBED_CACHE[pid2] = v
            batch = []
    if batch:
        vecs = _embed([t for _, t in batch])
        for (pid2, _), v in zip(batch, vecs):
            _EMBED_CACHE[pid2] = v
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(json.dumps(_EMBED_CACHE), encoding="utf-8")
    return _EMBED_CACHE


def tool_cerca(needles: list, root: Path) -> tuple[str, list[str]]:
    concepts = [str(n).strip() for n in (needles or []) if str(n).strip()]
    if not concepts:
        return "error: cerca requiere needles (conceptos 1–3 palabras)", []
    query = " ".join(concepts[:4])
    try:
        cache = _ensure_embed_cache(root)
        qv = _embed([query])[0]
    except Exception as e:  # noqa: BLE001
        return f"cerca falló (embed): {e}", []
    scored = sorted(
        ((_cos(qv, vec), pid) for pid, vec in cache.items()),
        reverse=True,
    )[:5]
    paths = [pid for _, pid in scored[:3]]
    lines = [
        f"cerca query=`{query}` (hipótesis; no veredicto)",
        "READABLE_PATHS (elige read de uno de estos; no inventes otros):",
    ]
    for i, (sc, pid) in enumerate(scored[:3], 1):
        lines.append(f"  {i}. `{pid}` score={sc:.3f}")
    if len(scored) > 3:
        lines.append("otros (menor prioridad):")
        for sc, pid in scored[3:]:
            lines.append(f"  - `{pid}` score={sc:.3f}")
    lines.append("Siguiente ola sugerida: read del path #1 o grep acotado a su directorio.")
    return "\n".join(lines), paths


def tool_atlas(query: str, root: Path) -> str:
    q = (query or "").strip() or "workspace"
    raw, paths = tool_cerca(q.split()[:4], root)
    if raw.startswith("cerca falló") or raw.startswith("error:"):
        return raw.replace("cerca", "atlas", 1)
    lines = ["# atlas (hipótesis de retrieval; no es veredicto)"]
    for p in paths:
        lines.append(f"- `{p}`")
    for ln in raw.splitlines()[1:]:
        if ln.startswith("READABLE") or ln.startswith("Siguiente"):
            continue
        lines.append(ln if ln.startswith(" ") or ln.startswith("otros") else "- " + ln)
    lines.append("Usa grep/read para confirmar; no cites M* como hecho.")
    return "\n".join(lines)


def close_pressure(
    step: int,
    max_steps: int,
    seen_paths: set[str],
    greps: int,
    reads: int,
) -> str:
    """Runtime pressure to close once the hunt has anchorage — not case-specific."""
    used = step + 1
    left = max_steps - used
    parts = [f"Olas {used}/{max_steps} (quedan {left})."]
    anchored = bool(seen_paths) and reads >= 1
    if anchored and used >= 3:
        parts.append(
            "Si ya puedes nombrar el objeto de TU consulta con path:symbol (o citar ausencia), "
            "cierra ahora (encontrado|parcial|no_encontrado). No hace falta agotar olas."
        )
    if left <= 3:
        parts.append("Presión alta: preferí cerrar con lo leído a otro grep de pesca.")
    if left <= 1:
        parts.append("Última ola útil: cierra con veredicto.")
    if left <= 2 and greps >= 3 and reads >= 2:
        parts.append("Ya hubo greps+reads suficientes; cierra.")
    return " ".join(parts)


def run_explorer(
    api: str,
    model: str,
    consulta: str,
    root: Path,
    log: list[str],
    tools: list[str] | None = None,
    max_steps: int = 10,
) -> dict:
    tools = tools or ["grep", "read"]
    notebook: list[str] = []
    seen_paths: set[str] = set()
    last_grep_key = ""
    messages = [
        {"role": "system", "content": explorer_system(tools)},
        {
            "role": "user",
            "content": (
                f"## Consulta\n{consulta}\n\n"
                f"Elige UNA ola ({'/'.join(tools)}/cerrar). Tope {max_steps} olas. "
                f"Cierra en cuanto puedas citar el mecanismo (o su ausencia); no agotes el tope por costumbre."
            ),
        },
    ]
    greps = reads = follows = entres = cercas = 0

    def follow_up(body: str, step: int) -> str:
        press = close_pressure(step, max_steps, seen_paths, greps, reads)
        return f"{body}\n\n{press}"

    for step in range(max_steps):
        raw = chat(api, model, messages)
        log.append(f"explorer_raw[{step}]: {raw[:800]}")
        try:
            ola = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": f"JSON inválido ({e}). Responde solo un objeto JSON válido.",
                }
            )
            continue
        do = (ola.get("do") or "").lower()
        if do == "grep" and "grep" in tools:
            left = max_steps - (step + 1)
            if left <= 2 and reads >= 1 and seen_paths:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": follow_up(
                            "Presión: no más grep de pesca con poca cuota. Cierra con lo anclado.",
                            step,
                        ),
                    }
                )
                continue
            if greps >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {"role": "user", "content": "Tope de greps. Cierra con veredicto."}
                )
                continue
            pat = str(ola.get("pattern") or ola.get("needle") or "")
            glob = ola.get("glob")
            gkey = f"{pat}||{glob or ''}"
            if gkey == last_grep_key:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": "grep repetido (mismo pattern+glob). Cambia pattern/glob o haz read/cerrar.",
                    }
                )
                continue
            last_grep_key = gkey
            greps += 1
            result = tool_grep(pat, glob, root)
            for p in paths_from_rg(result):
                seen_paths.add(p)
            notebook.append(f"### grep `{pat}`\n```\n{result[:6000]}\n```")
            path_note = (
                "paths anclados: " + ", ".join(f"`{p}`" for p in sorted(seen_paths)[:12])
                if seen_paths
                else "sin paths (0 hits)"
            )
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado grep\n```\n{result[:6000]}\n```\n"
                        f"{path_note}\n\nSiguiente ola (read solo de paths anclados).",
                        step,
                    ),
                }
            )
            continue
        if do == "read" and "read" in tools:
            if reads >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {"role": "user", "content": "Tope de reads. Cierra con veredicto."}
                )
                continue
            reads += 1
            path = ola.get("path") or ""
            result = tool_read(
                str(path),
                int(ola.get("offset") or 1),
                int(ola.get("limit") or 120),
                root,
                allowed=seen_paths,
            )
            if not result.startswith("error:"):
                resolved, _ = resolve_read_path(str(path), root, seen_paths)
                if resolved:
                    seen_paths.add(resolved)
            notebook.append(f"### read `{path}`\n```\n{result[:8000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado read\n```\n{result[:8000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "follow" and "follow" in tools:
            if follows >= 3:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {"role": "user", "content": "Tope de follow. Cierra o usa grep/read."}
                )
                continue
            follows += 1
            sym = ola.get("symbol") or ola.get("peek") or ""
            path = ola.get("path")
            result = tool_follow(str(sym), str(path) if path else None, root)
            for p in paths_from_rg(result):
                seen_paths.add(p)
            notebook.append(f"### follow `{sym}`\n```\n{result[:6000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado follow\n```\n{result[:6000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "entre" and "entre" in tools:
            if entres >= 2:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {"role": "user", "content": "Tope de entre. Cierra o usa grep/read."}
                )
                continue
            entres += 1
            result = tool_entre(str(ola.get("from") or ""), str(ola.get("to") or ""), root)
            for p in paths_from_rg(result):
                seen_paths.add(p)
            notebook.append(
                f"### entre `{ola.get('from')}` → `{ola.get('to')}`\n```\n{result[:5000]}\n```"
            )
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado entre\n```\n{result[:5000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "peek" and "peek" in tools:
            if reads >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de lecturas. Cierra."})
                continue
            reads += 1
            path = ola.get("path") or ""
            result = tool_read(
                str(path), int(ola.get("offset") or 1), 40, root, allowed=seen_paths
            )
            notebook.append(f"### peek `{path}`\n```\n{result[:4000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado peek\n```\n{result[:4000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "needles" and "needles" in tools:
            if greps >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de búsquedas. Cierra."})
                continue
            greps += 1
            result = tool_needles(ola.get("needles") or [], root)
            for p in paths_from_rg(result):
                seen_paths.add(p)
            notebook.append(f"### needles\n```\n{result[:6000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado needles\n```\n{result[:6000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "cerca" and "cerca" in tools:
            if cercas >= 1:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": "cerca ya usada (máx 1/job). Haz read de READABLE_PATHS o grep/cerrar.",
                    }
                )
                continue
            cercas += 1
            greps += 1
            result, paths = tool_cerca(ola.get("needles") or [], root)
            for p in paths:
                seen_paths.add(p)
            notebook.append(f"### cerca\n```\n{result[:4000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado cerca\n```\n{result[:4000]}\n```\n"
                        "OBLIGATORIO: siguiente ola = read de uno de READABLE_PATHS "
                        "(o grep con glob en ese directorio). No inventes paths.",
                        step,
                    ),
                }
            )
            continue
        if do == "atlas" and "atlas" in tools:
            if greps >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de atlas. Cierra."})
                continue
            greps += 1
            result = tool_atlas(str(ola.get("query") or consulta), root)
            for p in re.findall(r"`((?:src|tests)/[^`]+)`", result):
                seen_paths.add(p)
            notebook.append(f"### atlas\n```\n{result[:4000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado atlas\n```\n{result[:4000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "inbody" and "inbody" in tools:
            if greps >= 6:
                messages.append({"role": "assistant", "content": raw})
                messages.append({"role": "user", "content": "Tope de inbody. Cierra."})
                continue
            path = str(ola.get("path") or "")
            resolved, err = resolve_read_path(path, root, seen_paths)
            if err:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": follow_up(err + "\nSiguiente ola.", step),
                    }
                )
                continue
            greps += 1
            result = tool_inbody(resolved, str(ola.get("pattern") or ""), root)
            notebook.append(f"### inbody `{resolved}`\n```\n{result[:4000]}\n```")
            messages.append({"role": "assistant", "content": raw})
            messages.append(
                {
                    "role": "user",
                    "content": follow_up(
                        f"## Resultado inbody\n```\n{result[:4000]}\n```\n\nSiguiente ola.",
                        step,
                    ),
                }
            )
            continue
        if do == "cerrar":
            visto = []
            for n in notebook:
                m = re.search(
                    r"### (?:grep|read|follow|entre|peek|needles|cerca|atlas|inbody) `?([^`\n]+)`?",
                    n,
                )
                if m:
                    visto.append(m.group(1)[:120])
            for s in ola.get("simbolos") or []:
                if s not in visto:
                    visto.append(str(s)[:120])
            for p in sorted(seen_paths)[:8]:
                if p not in visto:
                    visto.append(p)
            raw_v = str(ola.get("veredicto") or "parcial").strip().lower()
            # Map legacy hay/no_hay to factual labels (hijo no vende el claim del usuario).
            vmap = {
                "hay": "encontrado",
                "no_hay": "no_encontrado",
                "no_concluyente": "parcial",
                "encontrado": "encontrado",
                "no_encontrado": "no_encontrado",
                "parcial": "parcial",
            }
            verd = vmap.get(raw_v, "parcial")
            # falta obligatorio (alias no_visto). Key must be present.
            if "falta" not in ola and "no_visto" not in ola:
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": follow_up(
                            "cerrar exige campo falta (lista; [] solo si cubriste toda TU consulta). "
                            'Ejemplo: {"do":"cerrar","veredicto":"parcial","simbolos":[],'
                            '"evidencia":[],"falta":["…"],"why":"…"}',
                            step,
                        ),
                    }
                )
                continue
            falta = ola.get("falta") if "falta" in ola else ola.get("no_visto")
            if isinstance(falta, str):
                falta = [falta] if falta.strip() else []
            if not isinstance(falta, list):
                messages.append({"role": "assistant", "content": raw})
                messages.append(
                    {
                        "role": "user",
                        "content": follow_up(
                            "falta debe ser una lista JSON (puedes usar []). Reemite cerrar.",
                            step,
                        ),
                    }
                )
                continue
            falta = [str(x)[:120] for x in falta if str(x).strip()][:6]
            # Forma: encontrado con falta no vacía → parcial (el hijo ya señaló hueco).
            if verd == "encontrado" and falta:
                verd = "parcial"
            return {
                "consulta": consulta,
                "visto": visto[:16],
                "cerrado": json.dumps(
                    {
                        "veredicto": verd,
                        "simbolos": ola.get("simbolos") or [],
                        "evidencia": ola.get("evidencia") or [],
                        "falta": falta,
                        "no_visto": falta,  # alias para notebooks viejos
                        "why": ola.get("why") or "",
                    },
                    ensure_ascii=False,
                ),
                "notebook": "\n\n".join(notebook),
                "why": ola.get("why") or "",
                "veredicto": verd,
                "falta": falta,
                "no_visto": falta,
            }
        messages.append({"role": "assistant", "content": raw})
        messages.append(
            {
                "role": "user",
                "content": (
                    f"do debe ser {'|'.join(tools)}|cerrar. "
                    'Ejemplo: {"do":"grep","pattern":"restore_workspace","why":"..."}'
                ),
            }
        )
    return {
        "consulta": consulta,
        "visto": sorted(seen_paths)[:16],
        "cerrado": json.dumps(
            {"veredicto": "no_concluyente", "why": "tope de pasos sin cerrar"},
            ensure_ascii=False,
        ),
        "notebook": "\n\n".join(notebook),
        "why": "tope de pasos",
        "veredicto": "no_concluyente",
    }



def run_case(
    api: str,
    model: str,
    case_id: str,
    claim: str,
    out_dir: Path,
    root: Path,
    tools: list[str] | None = None,
) -> dict:
    tools = tools or ["grep", "read"]
    out_dir.mkdir(parents=True, exist_ok=True)
    turns: list[dict] = []
    jobs: list[dict] = []
    notebooks: list[str] = []
    log: list[str] = []
    job_summaries: list[str] = []

    pilot_msgs = [
        {"role": "system", "content": PILOT_SYS},
        {
            "role": "user",
            "content": f"## Claim del usuario\n{claim}\n\nJobs hechos: 0/3.\nElige UNA acción.",
        },
    ]

    closed_why = ""
    closed_veredicto = ""
    for round_i in range(8):
        raw = chat(api, model, pilot_msgs)
        log.append(f"pilot_raw[{round_i}]: {raw[:800]}")
        try:
            act = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            pilot_msgs.append({"role": "assistant", "content": raw})
            pilot_msgs.append(
                {"role": "user", "content": f"JSON inválido ({e}). Solo un objeto JSON."}
            )
            continue
        do = (act.get("do") or "").lower()
        if do == "explorar":
            if len(jobs) >= 3:
                pilot_msgs.append({"role": "assistant", "content": raw})
                pilot_msgs.append(
                    {
                        "role": "user",
                        "content": "Ya hay 3 jobs. Solo puedes cerrar.",
                    }
                )
                continue
            consulta = (act.get("consulta") or "").strip()
            turns.append(
                {"do": "explorar", "consulta": consulta, "why": act.get("why") or ""}
            )
            job = run_explorer(api, model, consulta, root, log, tools=tools)
            jobs.append(
                {
                    "consulta": job["consulta"],
                    "visto": job["visto"],
                    "cerrado": job["cerrado"],
                }
            )
            notebooks.append(f"## Job {len(jobs)}: {consulta}\n\n{job['notebook']}")
            job_summaries.append(
                f"- job{len(jobs)} consulta={consulta!r} veredicto={job['veredicto']} why={job['why'][:200]}"
            )
            (out_dir / "jobs.md").write_text(
                "\n\n".join(
                    f"### Job {i+1}\nconsulta: {j['consulta']}\n\nCerrado:\n```\n{j['cerrado']}\n```\nvisto: {j['visto']}"
                    for i, j in enumerate(jobs)
                )
                + "\n",
                encoding="utf-8",
            )
            (out_dir / "notebook.md").write_text("\n\n".join(notebooks) + "\n", encoding="utf-8")
            pilot_msgs.append({"role": "assistant", "content": raw})
            pilot_msgs.append(
                {
                    "role": "user",
                    "content": (
                        f"## Resultado del hijo\n{job['cerrado']}\n"
                        f"visto: {job['visto']}\n\n"
                        f"Jobs hechos: {len(jobs)}/3.\n"
                        + "\n".join(job_summaries)
                        + "\n\nElige UNA acción (explorar o cerrar)."
                    ),
                }
            )
            continue
        if do == "cerrar":
            closed_why = act.get("why") or ""
            closed_veredicto = act.get("veredicto") or ""
            turns.append({"do": "cerrar", "why": closed_why})
            break
        pilot_msgs.append({"role": "assistant", "content": raw})
        pilot_msgs.append(
            {
                "role": "user",
                "content": 'do debe ser explorar|cerrar. Ejemplo: {"do":"explorar","consulta":"...","why":"..."}',
            }
        )
    else:
        closed_why = closed_why or "tope de rondas piloto"
        turns.append({"do": "cerrar", "why": closed_why, "error": "piloto_timeout"})

    control = {
        "cerró": True,
        "why": closed_why,
        "veredicto": closed_veredicto,
        "jobs_run": len(jobs),
        "induce": "miss-answer",
        "model": model,
        "harness": "explore_lite_local",
        "thinking": False,
        "tools": tools,
        "jobs": jobs,
        "turns": turns,
    }
    (out_dir / "control.json").write_text(
        json.dumps(control, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (out_dir / "turns.jsonl").write_text(
        "\n".join(json.dumps(t, ensure_ascii=False) for t in turns) + "\n",
        encoding="utf-8",
    )
    (out_dir / "lite.log").write_text("\n".join(log) + "\n", encoding="utf-8")
    return control


def load_claims() -> dict[str, str]:
    by_id = {c["id"]: c["prompt"] for c in json.loads(PROMPTS.read_text(encoding="utf-8"))}
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    out: dict[str, str] = {}
    for c in man["cases"]:
        if c.get("via") == "prompt":
            out[c["id"]] = c["prompt"]
        else:
            out[c["id"]] = by_id[c["id"]]
    return out


def parse_tools(s: str) -> list[str]:
    allowed = {
        "grep",
        "read",
        "follow",
        "entre",
        "peek",
        "needles",
        "cerca",
        "atlas",
        "inbody",
    }
    tools = [t.strip().lower() for t in s.split(",") if t.strip()]
    bad = [t for t in tools if t not in allowed]
    if bad:
        raise SystemExit(f"tools desconocidas: {bad}; usa {sorted(allowed)}")
    if "grep" not in tools or "read" not in tools:
        raise SystemExit("tools debe incluir al menos grep,read (suelo lite)")
    return tools


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default=DEFAULT_API)
    ap.add_argument("--model", default=DEFAULT_MODEL)
    ap.add_argument(
        "--out",
        type=Path,
        default=ROOT / ".tuide/ai/l2_wave/v2_night/H_LITE_T2/rep_1",
    )
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument(
        "--tools",
        default="grep,read",
        help="comma list: grep,read[,follow][,entre] — thinking always off",
    )
    ap.add_argument("--label", default="H_LITE_T2")
    args = ap.parse_args()
    tools = parse_tools(args.tools)
    claims = load_claims()
    man = json.loads(MANIFEST.read_text(encoding="utf-8"))
    cases = man["cases"]
    if args.case:
        want = set(args.case)
        cases = [c for c in cases if c["id"] in want]
    args.out.mkdir(parents=True, exist_ok=True)
    base = args.out.parent
    t_wall0 = time.time()
    (base / "STARTED.txt").write_text(
        f"{args.label} start {time.strftime('%Y-%m-%dT%H:%M:%S%z')}\n"
        f"api={args.api}\nmodel={args.model}\ntools={','.join(tools)}\nthinking=off\n",
        encoding="utf-8",
    )
    print(
        f"{args.label} out={args.out} api={args.api} model={args.model} "
        f"tools={tools} thinking=off",
        flush=True,
    )
    case_secs: dict[str, float] = {}
    for c in cases:
        cid = c["id"]
        run = args.out / cid
        if (run / "control.json").is_file():
            print(f"skip {cid}", flush=True)
            continue
        print(f"==== {cid} {time.strftime('%H:%M:%S')} ====", flush=True)
        t0 = time.time()
        try:
            run_case(args.api, args.model, cid, claims[cid], run, ROOT, tools=tools)
            dt = time.time() - t0
            case_secs[cid] = dt
            print(f"done {cid} in {dt:.0f}s", flush=True)
        except Exception as e:  # noqa: BLE001
            print(f"FAIL {cid}: {e}", flush=True)
            run.mkdir(parents=True, exist_ok=True)
            (run / "FAILED").write_text(str(e) + "\n", encoding="utf-8")
            raise

    wall_sec = time.time() - t_wall0
    subprocess.check_call([str(SCORE), str(args.out), str(MANIFEST)])
    sb = json.loads((args.out / "scoreboard.json").read_text(encoding="utf-8"))
    rows = sb.get("rows") or []
    oks = [1.0 if r.get("ok") else 0.0 for r in rows]
    ogs = [1.0 if r.get("ok_gold") else 0.0 for r in rows]
    summary = {
        "label": args.label,
        "model": args.model,
        "api": args.api,
        "thinking": False,
        "tools": tools,
        "ok_frac": sum(oks) / len(oks) if oks else 0.0,
        "ok_gold_frac": sum(ogs) / len(ogs) if ogs else 0.0,
        "wall_sec": round(wall_sec, 1),
        "case_secs": {k: round(v, 1) for k, v in case_secs.items()},
        "vs": {
            "H_LITE_ok": 0.80,
            "H_LITE_ok_gold": 0.80,
            "H_CURSOR_ok": 0.80,
            "S0_ok": 0.44,
            "T2_CODER80B_ok": 0.40,
        },
        "rows": [
            {
                "case": r.get("case"),
                "ok": r.get("ok"),
                "ok_gold": r.get("ok_gold"),
                "kind": r.get("kind"),
            }
            for r in rows
        ],
    }
    (base / f"{args.label}_SUMMARY.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    # also legacy name for run_h_lite compat
    (base / "H_LITE_T2_SUMMARY.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    (base / "DONE").write_text(time.strftime("%Y-%m-%dT%H:%M:%S%z") + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
