#!/usr/bin/env python3
"""Synthetic: isolate the EDIT stage of admin_v1 (chunk emission → apply → compile → feedback).

Everything BEFORE the edit decision is seeded from a frozen exploration result
(a real SUMMARY.json from a previous battery run) — we never re-run the real
explorer. That covers "inflar las evidencias de exploración".

What this script adds that no existing tools/l2_wave/*.py script does: after
`confirmar_editar`, every existing probe (probe_admin_pilot.py, CONFIRM_EDIT_USER)
treats the confirmation itself as the terminal event. Nothing ever asks the
model for the real edit payload. In production (admin_system_prompt(),
src/ai/l2_admin.cpp:4606-4614) the pilot is supposed to follow confirmation
with a separate turn: `{"do":"spawn","spawn":{"tipo":"edit","arg":path,
"search":…,"replace":…}}`. This script is the first thing that actually asks
for it, applies it, compiles it, and feeds the result back.

Pipeline:
  1. seed         — load jobs[] from a frozen SUMMARY.json (real exploration, not replayed)
  2. editar       — pilot decides do=editar (menu restricted: editar|cerrar|ask_user,
                     no spawn — exploration is already "spent" by design)
  3. confirmar_editar — reuses CONFIRM_EDIT_USER verbatim from probe_admin_pilot.py
  4. emit hunk    — NEW turn (EMIT_EDIT_USER) asking for spawn{tipo:edit,arg,search,replace}
  5. apply        — Python port of apply_hunk_to_text (src/ai/search_replace.cpp:396-551):
                     escape-noise normalization, exact unique-span match, flex-normalized
                     fallback. Applied to an in-memory copy, never the real working tree.
  6. compile      — the REAL compiler invocation for that exact file, read from
                     build/compile_commands.json, with -c/-o swapped for -fsyntax-only
                     and the source path swapped for a scratch copy. Real toolchain,
                     real flags, no link — fast because it's already a single-TU command.
  7. feedback     — apply/compile errors are clipped and fed back as the pilot's next
                     user turn (mirrors admin_shell_enrich_result's error→facts path);
                     the pilot gets `--max-retries` attempts to converge.

Usage:
  python3 tools/l2_wave/edit_lite_local.py \
      --seed .tuide/ai/l2_admin_probe/qwen14b_edit_check_20260927T200421Z/092_edit
"""
from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))


def _find_repo_root() -> Path:
    """The real checkout that has build/compile_commands.json.

    When this script runs from a git worktree (.claude/worktrees/<name>/…),
    the worktree has no build/ of its own — compile_commands.json (with its
    absolute paths) only exists in the main checkout. Reading the file being
    edited and compiling it must both happen against that same tree.
    """
    parts = ROOT.parts
    if ".claude" in parts:
        return Path(*parts[: parts.index(".claude")])
    return ROOT


REPO_ROOT = _find_repo_root()

from probe_admin_pilot import (  # noqa: E402
    CONFIRM_EDIT_USER,
    chat,
    extract_json,
    notebook_md,
)

DEFAULT_SEED = (
    REPO_ROOT
    / ".tuide/ai/l2_admin_probe/qwen14b_edit_check_20260927T200421Z"
    / "092_edit"
)

EDIT_SYS = """Eres el PILOTO de TIDE. La exploración para esta consulta ya terminó —
el NOTEBOOK de abajo ya tiene toda la evidencia que vas a tener disponible (no hay
spawn explore en esta sonda, no va a aparecer nada nuevo por buscar más).

Tu única decisión ahora es UNA de estas tres, un JSON por turno, sin prosa fuera del JSON:
{"action":"admin_v1","do":"editar","why":"…"}
{"action":"admin_v1","do":"cerrar","why":"…","reply":"…"}
{"action":"admin_v1","do":"ask_user","why":"…","reply":"…"}

- editar: el NOTEBOOK ya te dice qué archivo y qué símbolo hay que cambiar; pide editar.
- cerrar: el pedido no requiere ningún cambio de código (ya está como se pide).
- ask_user: solo si falta un dato que NINGÚN archivo del notebook puede darte.
"""

EMIT_EDIT_USER = """## Emitir la edición
Confirmaste la edición. Ahora emití el spawn real — el runtime lo aplica tal cual
contra el archivo y después compila. Un solo archivo, un solo bloque search/replace:

{"action":"admin_v1","do":"spawn","why":"…",
 "spawn":{"tipo":"edit","arg":"path/relativo/al/workspace","search":"texto EXACTO tal como aparece en el archivo","replace":"texto nuevo"}}

Reglas:
- "arg" tiene que ser uno de los paths que ya aparecen en el NOTEBOOK (si no, se rechaza).
- "search" tiene que matchear un único lugar del archivo (si aparece 0 veces o ≥2 veces, se rechaza).
- No hagas diffs ni marques líneas con +/- ni uses ```: search/replace son texto plano tal cual va en el archivo.
"""

RETRY_AFTER_ERROR = """## El runtime rechazó tu edición
{error}

Reintentá con el mismo formato (spawn tipo=edit, un archivo, un search/replace):
{{"action":"admin_v1","do":"spawn","why":"…","spawn":{{"tipo":"edit","arg":"…","search":"…","replace":"…"}}}}
"""


# ---------------------------------------------------------------------------
# Port of src/ai/search_replace.cpp (apply_hunk_to_text and its helpers).
# Kept behaviorally identical on purpose: this is what lets the synth score
# "did the model's hunk actually apply" the same way production would.
# ---------------------------------------------------------------------------


def normalize_hunk_escape_noise(text: str) -> str:
    out: list[str] = []
    i, n = 0, len(text)
    while i < n:
        if text[i] == "\\" and i + 1 < n:
            c = text[i + 1]
            if c == "s":
                out.append("\n")
                i += 2
                if i < n and text[i] == "*":
                    i += 1
                continue
            if c == "n":
                out.append("\n")
                i += 2
                continue
            if c == "t":
                out.append("\t")
                i += 2
                continue
        out.append(text[i])
        i += 1
    return "".join(out)


def flex_normalize(s: str) -> tuple[str, list[int]]:
    """Collapse CRLF, strip trailing ws per line, collapse blank-line runs to one \\n."""
    out_chars: list[str] = []
    to_orig: list[int] = []
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

    i = 0
    while i < n:
        line_start = i
        content_end = i
        has_content = False
        while i < n and s[i] not in "\n\r":
            if s[i] not in " \t":
                has_content = True
                content_end = i + 1
            i += 1
        eol_at = i
        if has_content:
            for k in range(line_start, content_end):
                out_chars.append(s[k])
                to_orig.append(k)
            if eol_at < n:
                out_chars.append("\n")
                to_orig.append(eol_at)
                i = skip_eol(eol_at)
                while i < n:
                    t = i
                    while t < n and s[t] in " \t":
                        t += 1
                    if t < n and s[t] in "\n\r":
                        i = skip_eol(t)
                        continue
                    break
        else:
            if eol_at < n:
                i = skip_eol(eol_at)
            else:
                break
    return "".join(out_chars), to_orig


def find_unique_span_exact(haystack: str, needle: str) -> tuple[tuple[int, int] | None, str]:
    if not needle:
        return None, "search vacío"
    first = haystack.find(needle)
    if first == -1:
        return None, "search no encontrado (0 matches)"
    second = haystack.find(needle, first + len(needle))
    if second != -1:
        return None, "search ambiguo (≥2 matches)"
    return (first, first + len(needle)), ""


def find_unique_span_flex(haystack: str, needle: str) -> tuple[tuple[int, int] | None, str]:
    if not needle.strip():
        return None, "search vacío"
    h_text, h_map = flex_normalize(haystack)
    n_text, _ = flex_normalize(needle)
    if not n_text:
        return None, "search vacío"
    first = h_text.find(n_text)
    if first == -1:
        return None, "search no encontrado (0 matches)"
    second = h_text.find(n_text, first + len(n_text))
    if second != -1:
        return None, "search ambiguo (≥2 matches)"
    norm_end = first + len(n_text)
    byte_begin = h_map[first]
    byte_end = h_map[norm_end] if norm_end < len(h_map) else len(haystack)
    return (byte_begin, byte_end), ""


def find_unique_span_allow_flex(haystack: str, needle: str) -> tuple[tuple[int, int] | None, str]:
    span, err = find_unique_span_exact(haystack, needle)
    if span is not None:
        return span, ""
    if "0 matches" not in err:
        return None, err
    return find_unique_span_flex(haystack, needle)


def apply_hunk_to_text(text: str, search: str, replace: str) -> dict:
    search_n = normalize_hunk_escape_noise(search)
    replace_n = normalize_hunk_escape_noise(replace)
    span, err = find_unique_span_allow_flex(text, search_n)
    if span is None:
        return {"ok": False, "error": err, "search_norm": search_n}
    b, e = span
    after = text[:b] + replace_n + text[e:]
    return {
        "ok": True,
        "after": after,
        "old_text": text[b:e],
        "search_norm": search_n,
        "replace_norm": replace_n,
    }


# ---------------------------------------------------------------------------
# Real compile, scoped to the touched file (build/compile_commands.json entry
# with -c/-o swapped for -fsyntax-only and the source path pointed at scratch).
# ---------------------------------------------------------------------------


def load_compile_entry(rel_path: str) -> dict | None:
    ccj = REPO_ROOT / "build" / "compile_commands.json"
    if not ccj.is_file():
        return None
    entries = json.loads(ccj.read_text(encoding="utf-8"))
    target = str((REPO_ROOT / rel_path).resolve())
    for e in entries:
        if str(Path(e["file"]).resolve()) == target:
            return e
    return None


def _include_roots(tokens: list[str]) -> list[Path]:
    roots = []
    i = 0
    while i < len(tokens):
        t = tokens[i]
        if t == "-I" and i + 1 < len(tokens):
            roots.append(Path(tokens[i + 1]))
            i += 2
            continue
        if t.startswith("-I") and len(t) > 2:
            roots.append(Path(t[2:]))
        i += 1
    return roots


def find_header_proxy(rel_path: str) -> tuple[dict, Path] | None:
    """A header has no compile_commands.json entry of its own. Find a sibling
    .cpp that IS a translation unit (same stem, same dir — e.g. l2_admin.hpp
    -> l2_admin.cpp) and the -I root its own #include resolves through, so we
    can compile-check the header by injecting a scratch copy ahead of it."""
    ccj = REPO_ROOT / "build" / "compile_commands.json"
    if not ccj.is_file():
        return None
    entries = json.loads(ccj.read_text(encoding="utf-8"))
    header_abs = (REPO_ROOT / rel_path).resolve()
    for suffix in (".cpp", ".cc"):
        sibling = header_abs.with_suffix(suffix)
        for e in entries:
            if Path(e["file"]).resolve() == sibling:
                tokens = shlex.split(e["command"])
                for root in _include_roots(tokens):
                    root_r = root.resolve()
                    try:
                        rel_to_root = header_abs.relative_to(root_r)
                    except ValueError:
                        continue
                    return e, rel_to_root
    return None


def compile_scratch(rel_path: str, new_text: str, out_dir: Path) -> dict:
    entry = load_compile_entry(rel_path)
    if entry is not None:
        scratch = out_dir / "scratch" / Path(rel_path).name
        scratch.parent.mkdir(parents=True, exist_ok=True)
        scratch.write_text(new_text, encoding="utf-8")
        tokens = shlex.split(entry["command"])
        if "-o" in tokens:
            oi = tokens.index("-o")
            del tokens[oi : oi + 2]
        if "-c" in tokens:
            tokens[tokens.index("-c")] = "-fsyntax-only"
        else:
            tokens.append("-fsyntax-only")
        tokens[-1] = str(scratch)
        run_dir = entry.get("directory", str(REPO_ROOT / "build"))
    else:
        proxy = find_header_proxy(rel_path)
        if proxy is None:
            return {"ok": None, "reason": f"sin entry (ni proxy .cpp) en compile_commands.json para {rel_path}"}
        proxy_entry, rel_to_root = proxy
        scratch_root = out_dir / "scratch_include"
        scratch_header = scratch_root / rel_to_root
        scratch_header.parent.mkdir(parents=True, exist_ok=True)
        scratch_header.write_text(new_text, encoding="utf-8")
        tokens = shlex.split(proxy_entry["command"])
        if "-o" in tokens:
            oi = tokens.index("-o")
            del tokens[oi : oi + 2]
        if "-c" in tokens:
            tokens[tokens.index("-c")] = "-fsyntax-only"
        else:
            tokens.append("-fsyntax-only")
        # -I scratch_root ahead of everything else so #include picks up the
        # edited header instead of the real one on disk.
        tokens = [tokens[0], "-I", str(scratch_root)] + tokens[1:]
        run_dir = proxy_entry.get("directory", str(REPO_ROOT / "build"))
        scratch = scratch_header

    proc = subprocess.run(
        tokens,
        cwd=run_dir,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    out = (proc.stdout or "") + (proc.stderr or "")
    return {
        "ok": proc.returncode == 0,
        "returncode": proc.returncode,
        "log_tail": out[-2000:],
        "scratch_path": str(scratch),
        "cmd": tokens,
    }


# ---------------------------------------------------------------------------
# Seed loading — reuse a frozen SUMMARY.json's jobs[] verbatim (no re-explore).
# ---------------------------------------------------------------------------


def _salvage_summary_head(head: str) -> dict:
    """summary_head is a *head* — a fixed-length prefix of the explorer's JSON
    verdict, often truncated mid-string (usually inside "why", which comes
    last). A plain json.loads silently loses "evidencia"/"simbolos" too even
    though they appear earlier and are intact — recover them by regex before
    giving up, so a truncated why doesn't blind the pilot to real evidence."""
    try:
        return json.loads(head)
    except (json.JSONDecodeError, TypeError):
        pass
    out: dict = {}
    for key in ("evidencia", "simbolos", "falta", "no_visto"):
        m = re.search(rf'"{key}"\s*:\s*(\[[^\]]*\])', head)
        if m:
            try:
                out[key] = json.loads(m.group(1))
            except json.JSONDecodeError:
                pass
    m = re.search(r'"why"\s*:\s*"(.*)', head, re.S)
    if m:
        out["why"] = m.group(1) + " …[clip]"
    return out


def load_seed_jobs(seed_dir: Path) -> tuple[str, list[dict], str, str]:
    summary_path = seed_dir / "SUMMARY.json"
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    prompt = summary.get("prompt") or ""
    api = summary.get("api") or "http://192.168.64.1:8080/v1"
    model = summary.get("model") or ""
    jobs = []
    for j in summary.get("jobs") or []:
        j = dict(j)
        parsed = _salvage_summary_head(j.get("summary_head") or "")
        if not j.get("evidencia"):
            j["evidencia"] = parsed.get("evidencia") or []
        if not j.get("why"):
            j["why"] = parsed.get("why") or ""
        if not j.get("falta"):
            j["falta"] = parsed.get("falta") or []
        jobs.append(j)
    return prompt, jobs, api, model


def notebook_paths(jobs: list[dict]) -> set[str]:
    paths: set[str] = set()
    for j in jobs:
        for p in j.get("paths") or []:
            paths.add(p.split(":")[0])
        for s in j.get("simbolos") or []:
            paths.add(str(s).split(":")[0])
    return paths


def detect_model(api: str) -> str:
    with urllib.request.urlopen(api.rstrip("/") + "/models", timeout=10) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    models = data.get("data") or data.get("models") or []
    if not models:
        raise RuntimeError(f"sin modelos cargados en {api}")
    return models[0].get("id") or models[0].get("name")


def clip(s: str, n: int = 900) -> str:
    s = s or ""
    return s if len(s) <= n else s[: n - 20] + " …[clip]… " + s[-20:]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=Path, default=DEFAULT_SEED)
    ap.add_argument("--api", default=None)
    ap.add_argument("--model", default=None)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--max-turns", type=int, default=10)
    ap.add_argument("--max-retries", type=int, default=2, help="retries on apply/compile failure")
    args = ap.parse_args()

    prompt, jobs, seed_api, seed_model = load_seed_jobs(args.seed)
    api = args.api or seed_api
    model = args.model or seed_model or detect_model(api)
    print(f"SEED prompt={prompt!r} jobs={len(jobs)} api={api} model={model}", flush=True)

    out = args.out or (
        REPO_ROOT / ".tuide/ai/l2_admin_probe" / f"edit_synth_{time.strftime('%Y%m%d_%H%M%S')}"
    )
    out.mkdir(parents=True, exist_ok=True)

    valid_paths = notebook_paths(jobs)
    user0 = (
        f"## Consulta\n{prompt}\n\n"
        f"## NOTEBOOK (exploración ya cerrada)\n{notebook_md(jobs)}\n\n"
        "Elige UNA acción (JSON admin_v1)."
    )
    msgs = [
        {"role": "system", "content": EDIT_SYS},
        {"role": "user", "content": user0},
    ]
    (out / "seed_user.md").write_text(user0, encoding="utf-8")

    turns: list[dict] = []
    stage = "menu"  # menu -> confirm -> emit -> apply/compile retries -> done
    attempts_left = args.max_retries + 1
    scratch_texts: dict[str, str] = {}
    result: dict = {"ok": False, "stage": "menu"}
    t0 = time.time()

    for turn_i in range(args.max_turns):
        raw = chat(api, model, msgs, max_tokens=700)
        print(f"-- turn {turn_i} [{stage}] --\n{raw[:400]}", flush=True)
        turns.append({"turn": turn_i, "stage": stage, "raw": raw})
        try:
            act = extract_json(raw)
        except ValueError as e:
            print(f"-- JSON INVALIDO (no cuenta contra el presupuesto de reintentos): {e}", flush=True)
            msgs.append({"role": "assistant", "content": raw})
            msgs.append({"role": "user", "content": f"JSON inválido: {e}. Reemití el JSON solo."})
            continue
        do = (act.get("do") or "").strip().lower()
        msgs.append({"role": "assistant", "content": raw})

        if stage == "menu":
            if do == "cerrar":
                result = {"ok": True, "stage": "cerrar", "reply": act.get("reply")}
                break
            if do == "ask_user":
                result = {"ok": False, "stage": "ask_user", "reply": act.get("reply")}
                break
            if do != "editar":
                msgs.append({"role": "user", "content": "Usa editar|cerrar|ask_user."})
                continue
            msgs.append({"role": "user", "content": CONFIRM_EDIT_USER})
            stage = "confirm"
            continue

        if stage == "confirm":
            if do == "cerrar":
                result = {"ok": True, "stage": "cerrar_from_confirm", "reply": act.get("reply")}
                break
            if do == "seguir_explorando":
                msgs.append(
                    {"role": "user", "content": "No hay spawn en esta sonda. confirmar_editar o cerrar."}
                )
                continue
            if do != "confirmar_editar":
                msgs.append({"role": "user", "content": "Usa confirmar_editar|cerrar."})
                continue
            cubre = (act.get("cubre") or "").strip()
            if len(cubre) < 4:
                msgs.append({"role": "user", "content": "confirmar_editar exige cubre no vacío."})
                continue
            msgs.append({"role": "user", "content": EMIT_EDIT_USER})
            stage = "emit"
            continue

        if stage == "emit":
            if do != "spawn" or not isinstance(act.get("spawn"), dict):
                print(f"-- EMIT FAIL: do={do!r} sin spawn dict (consume presupuesto)", flush=True)
                msgs.append({"role": "user", "content": 'Emití {"do":"spawn","spawn":{"tipo":"edit",...}}.'})
                attempts_left -= 1
                if attempts_left <= 0:
                    result = {"ok": False, "stage": "no_hunk_emitted"}
                    break
                continue
            spawn = act["spawn"]
            if (spawn.get("tipo") or "").strip().lower() != "edit":
                print(f"-- EMIT FAIL: spawn.tipo={spawn.get('tipo')!r} != edit (consume presupuesto)", flush=True)
                msgs.append({"role": "user", "content": "spawn.tipo debe ser \"edit\"."})
                attempts_left -= 1
                if attempts_left <= 0:
                    result = {"ok": False, "stage": "wrong_spawn_tipo"}
                    break
                continue
            arg = (spawn.get("arg") or "").strip()
            search = spawn.get("search") or ""
            replace = spawn.get("replace") or ""
            hunk_record = {"arg": arg, "search": search, "replace": replace}

            if arg not in valid_paths:
                err = f"edit: path no anclado en notebook: {arg!r} (paths válidos: {sorted(valid_paths)})"
                attempts_left -= 1
                print(f"-- APPLY FAIL: {err}", flush=True)
                if attempts_left <= 0:
                    result = {"ok": False, "stage": "path_not_in_notebook", "hunk": hunk_record}
                    break
                msgs.append({"role": "user", "content": RETRY_AFTER_ERROR.format(error=err)})
                continue

            base_text = scratch_texts.get(arg)
            if base_text is None:
                base_text = (REPO_ROOT / arg).read_text(encoding="utf-8", errors="replace")

            applied = apply_hunk_to_text(base_text, search, replace)
            if not applied["ok"]:
                attempts_left -= 1
                print(f"-- APPLY FAIL: {applied['error']}", flush=True)
                if attempts_left <= 0:
                    result = {
                        "ok": False,
                        "stage": "apply_failed",
                        "hunk": hunk_record,
                        "error": applied["error"],
                    }
                    break
                msgs.append(
                    {"role": "user", "content": RETRY_AFTER_ERROR.format(error=applied["error"])}
                )
                continue

            print(f"-- APPLY OK: {clip(applied['old_text'], 200)!r} -> {clip(applied['replace_norm'], 200)!r}", flush=True)
            comp = compile_scratch(arg, applied["after"], out)
            (out / f"compile_attempt{args.max_retries - attempts_left + 1}.json").write_text(
                json.dumps({"hunk": hunk_record, "apply": applied, "compile": comp}, ensure_ascii=False, indent=2),
                encoding="utf-8",
            )
            if comp["ok"] is None:
                # No compile_commands.json entry for this file — record apply success, skip compile.
                scratch_texts[arg] = applied["after"]
                result = {
                    "ok": True,
                    "stage": "apply_ok_no_compile_entry",
                    "hunk": hunk_record,
                    "reason": comp["reason"],
                }
                break
            if not comp["ok"]:
                attempts_left -= 1
                print(f"-- COMPILE FAIL:\n{clip(comp['log_tail'], 800)}", flush=True)
                if attempts_left <= 0:
                    result = {
                        "ok": False,
                        "stage": "compile_failed",
                        "hunk": hunk_record,
                        "log_tail": comp["log_tail"],
                    }
                    break
                msgs.append(
                    {
                        "role": "user",
                        "content": RETRY_AFTER_ERROR.format(
                            error="Compiló con error:\n" + clip(comp["log_tail"], 1200)
                        ),
                    }
                )
                continue

            scratch_texts[arg] = applied["after"]
            print("-- COMPILE OK", flush=True)
            result = {
                "ok": True,
                "stage": "compiled",
                "hunk": hunk_record,
                "old_text": applied["old_text"],
                "new_text": applied["replace_norm"],
                "attempts_used": args.max_retries + 1 - attempts_left,
            }
            break

    wall = time.time() - t0
    summary = {
        "prompt": prompt,
        "seed": str(args.seed),
        "api": api,
        "model": model,
        "wall_sec": wall,
        "result": result,
        "turns": turns,
    }
    (out / "SUMMARY.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"\n== RESULT == {json.dumps(result, ensure_ascii=False)[:500]}", flush=True)
    print(f"out={out}", flush=True)


if __name__ == "__main__":
    main()
