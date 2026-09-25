#!/usr/bin/env python3
"""Probe: after a frozen explore Cerrado, what exit does the pilot choose?

Skips running explore. Seeds the notebook with a prior explore job result, then
offers: spawn (search/read/explore), cerrar, editar.

editar does NOT exit immediately: runtime asks a confirmation pass
(confirmar_editar | seguir_explorando | cerrar) so the pilot must state
coverage vs gap before edit is accepted.
"""

from __future__ import annotations

import argparse
import json
import re
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ADMIN_SYS = """Eres el PILOTO de TIDE. Has recibido la orden del usuario.
NO lees código tú. Ya hay evidencia de un hijo explorador en el NOTEBOOK.
Cada turno UN JSON:
{"action":"admin_v1","do":"spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user","why":"…",
 "spawn":{"tipo":"…","brief":"…","arg":"…"},
 "cubre":"…","falta":"…","reply":"…"}

Salidas:
- cerrar: el prompt del usuario YA está atendido. Obliga "reply".
- editar: pides pasar a edición. El runtime te pedirá CONFIRMACIÓN (no edita aún).
- confirmar_editar: solo tras ese pedido. Obliga "cubre" (qué del pedido cubre el notebook)
  y "falta" (qué falta, o la palabra nada). "reply" opcional.
- seguir_explorando: tras el pedido de confirmación, si falta cazar; brief del hueco en spawn.
- spawn: explore|search|read (en esta sonda explore/search/read pueden estar fijados).
- ask_user: orden vaga.

Elige UNA acción."""

CONFIRM_USER = """## Confirmación de edición
Has pedido editar. Antes de entrar en edición, declara cobertura del pedido del usuario
respecto al NOTEBOOK (no inventes archivos no listados).

Elige UNA:
{"action":"admin_v1","do":"confirmar_editar","why":"…","cubre":"qué del pedido ya cubre el notebook","falta":"qué falta (o nada)","reply":"…"}
{"action":"admin_v1","do":"seguir_explorando","why":"…","spawn":{"tipo":"explore","brief":"hueco concreto a cazar"}}
{"action":"admin_v1","do":"cerrar","why":"…","reply":"mensaje al usuario"}
"""


def chat(api: str, model: str, messages: list[dict], max_tokens: int = 900) -> str:
    body = json.dumps(
        {
            "model": model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": 0.1,
            "chat_template_kwargs": {"enable_thinking": False},
            "enable_thinking": False,
        }
    ).encode("utf-8")
    req = urllib.request.Request(
        api.rstrip("/") + "/chat/completions",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=600) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    return (data["choices"][0]["message"].get("content") or "").strip()


def extract_json(text: str) -> dict:
    m = re.search(r"\{[\s\S]*\}", text)
    if not m:
        raise ValueError(f"no JSON in: {text[:400]}")
    return json.loads(m.group(0))


def normalize_act(act: dict) -> dict:
    a = dict(act)
    action = (a.get("action") or "").strip()
    do = (a.get("do") or "").strip().lower()
    tipos = {"explore", "search", "read", "edit", "build", "git", "shell"}
    if action == "spawn" and do in tipos:
        spawn = a.get("spawn") if isinstance(a.get("spawn"), dict) else {}
        spawn = dict(spawn)
        if not spawn.get("tipo"):
            spawn["tipo"] = do
        return {
            "action": "admin_v1",
            "do": "spawn",
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": spawn,
            "cubre": a.get("cubre") or "",
            "falta": a.get("falta") or "",
        }
    if action == "spawn" and do == "spawn" and isinstance(a.get("spawn"), dict):
        return {
            "action": "admin_v1",
            "do": "spawn",
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": a["spawn"],
            "cubre": a.get("cubre") or "",
            "falta": a.get("falta") or "",
        }
    exits = (
        "editar",
        "cerrar",
        "ask_user",
        "confirmar_editar",
        "seguir_explorando",
    )
    if action in exits or do in exits:
        use_do = do if do in exits else action
        return {
            "action": "admin_v1",
            "do": use_do,
            "why": a.get("why") or "",
            "reply": a.get("reply") or "",
            "spawn": a.get("spawn") if isinstance(a.get("spawn"), dict) else {},
            "cubre": str(a.get("cubre") or "").strip(),
            "falta": str(a.get("falta") or "").strip(),
        }
    if not action and do in ("cerrar", "editar", "ask_user", "spawn", *exits):
        a["action"] = "admin_v1"
    return a


def parse_admin(act: dict, *, awaiting_confirm: bool) -> tuple[dict | None, str]:
    act = normalize_act(act)
    action = (act.get("action") or "").strip()
    if action and action != "admin_v1":
        return None, "contrato admin_v1 inválido (action=admin_v1)"
    do = (act.get("do") or "").strip().lower()
    allowed = {
        "spawn",
        "cerrar",
        "editar",
        "ask_user",
        "confirmar_editar",
        "seguir_explorando",
    }
    if do not in allowed:
        return None, "do inválido (spawn|cerrar|editar|confirmar_editar|seguir_explorando|ask_user)"
    if awaiting_confirm and do not in (
        "confirmar_editar",
        "seguir_explorando",
        "cerrar",
        "ask_user",
    ):
        return None, "en confirmación de edición: confirmar_editar|seguir_explorando|cerrar|ask_user"
    if not awaiting_confirm and do in ("confirmar_editar", "seguir_explorando"):
        return None, "confirmar_editar/seguir_explorando solo tras pedir editar"
    why = (act.get("why") or "").strip()
    if len(why) < 4:
        return None, "why demasiado corto"
    reply = (act.get("reply") or "").strip()
    cubre = (act.get("cubre") or "").strip()
    falta = (act.get("falta") or "").strip()
    if do == "cerrar" and len(reply) < 8:
        return None, "cerrar exige reply (mensaje de respuesta al usuario)"
    if do == "confirmar_editar":
        if len(cubre) < 4:
            return None, "confirmar_editar exige cubre (qué del pedido cubre el notebook)"
        if len(falta) < 1:
            return None, "confirmar_editar exige falta (qué falta, o la palabra nada)"
    if do == "seguir_explorando":
        spawn = act.get("spawn") if isinstance(act.get("spawn"), dict) else {}
        brief = (spawn.get("brief") or "").strip()
        if len(brief) < 4:
            return None, "seguir_explorando exige spawn.brief = hueco a cazar"
        return {
            "do": do,
            "why": why,
            "reply": reply,
            "cubre": cubre,
            "falta": falta,
            "spawn": {"tipo": "explore", "brief": brief, "arg": (spawn.get("arg") or "").strip()},
        }, ""
    if do == "spawn":
        spawn = act.get("spawn")
        if not isinstance(spawn, dict):
            return None, "spawn sin objeto"
        tipo = (spawn.get("tipo") or "").strip().lower()
        if tipo not in ("explore", "search", "read"):
            return None, "spawn.tipo inválido (explore|search|read)"
        return {
            "do": "spawn",
            "why": why,
            "spawn": {
                "tipo": tipo,
                "brief": (spawn.get("brief") or "").strip(),
                "arg": (spawn.get("arg") or "").strip(),
            },
            "reply": reply,
            "cubre": cubre,
            "falta": falta,
        }, ""
    return {
        "do": do,
        "why": why,
        "reply": reply,
        "cubre": cubre,
        "falta": falta,
    }, ""


def notebook_from_explore(job: dict) -> str:
    cerrado = job.get("cerrado") or ""
    try:
        cobj = json.loads(cerrado) if isinstance(cerrado, str) else cerrado
        cerrado_pretty = json.dumps(cobj, ensure_ascii=False, indent=2)
    except json.JSONDecodeError:
        cerrado_pretty = str(cerrado)
        cobj = {}
    visto = job.get("visto") or []
    no_visto = job.get("no_visto") or cobj.get("no_visto") or []
    return (
        "### job1 tipo=explore\n"
        f"consulta: {job.get('consulta')}\n"
        f"veredicto: {job.get('veredicto') or cobj.get('veredicto')}\n"
        f"visto: {visto}\n"
        f"no_visto: {no_visto}\n"
        f"Cerrado:\n```\n{cerrado_pretty}\n```\n"
        f"why hijo: {job.get('why') or cobj.get('why') or ''}\n"
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://192.168.64.1:8080/v1")
    ap.add_argument("--model", default="qwen3.6-27b-q8_0")
    ap.add_argument(
        "--explore-json",
        default=str(
            ROOT
            / ".tuide/ai/l2_admin_probe/case18_novisto_20260923_203038/explore_job.json"
        ),
    )
    ap.add_argument(
        "--prompt",
        default=(
            "quiero que los errores de compilación que aparecen en la consola inferior "
            "también se marquen con una línea roja en el margen izquierdo del editor "
            "en la línea exacta donde está el error"
        ),
    )
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-turns", type=int, default=6)
    ap.add_argument(
        "--seed-editar",
        action="store_true",
        help="Arranca ya en ciclo de confirmación (simula que el piloto emitió editar).",
    )
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    job = json.loads(Path(args.explore_json).read_text(encoding="utf-8"))
    nb = notebook_from_explore(job)

    if args.seed_editar:
        user0 = (
            f"## Consulta\n{args.prompt}\n\n"
            f"## NOTEBOOK (resultado del hijo explorador)\n{nb}\n\n"
            + CONFIRM_USER
        )
        awaiting0 = True
    else:
        user0 = (
            f"## Consulta\n{args.prompt}\n\n"
            f"## Presupuesto\nspawns explore ya hechos=1 (esta sonda NO relanza explore "
            f"salvo seguir_explorando; preferible cerrar|editar)\n"
            f"do legales: spawn cerrar editar ask_user\n\n"
            f"## Sesion UI\n(sin UI)\n\n"
            f"## NOTEBOOK (resultado del hijo explorador)\n{nb}\n\n"
            f"Elige UNA acción (JSON admin_v1)."
        )
        awaiting0 = False
    msgs = [
        {"role": "system", "content": ADMIN_SYS},
        {"role": "user", "content": user0},
    ]

    turns: list[dict] = []
    t0 = time.time()
    final = None
    awaiting_confirm = awaiting0
    for turn in range(args.max_turns):
        raw = chat(args.api, args.model, msgs)
        (out / "turns.jsonl").open("a", encoding="utf-8").write(
            json.dumps(
                {"turn": turn, "raw": raw, "awaiting_confirm": awaiting_confirm},
                ensure_ascii=False,
            )
            + "\n"
        )
        print(
            f"==== turn {turn} confirm={awaiting_confirm} ====",
            flush=True,
        )
        print(raw[:1500], flush=True)
        try:
            act = extract_json(raw)
        except Exception as e:  # noqa: BLE001
            msgs.append({"role": "assistant", "content": raw})
            msgs.append({"role": "user", "content": f"JSON inválido ({e}). Solo admin_v1."})
            turns.append({"turn": turn, "error": str(e), "raw": raw[:800]})
            continue
        parsed, perr = parse_admin(act, awaiting_confirm=awaiting_confirm)
        turns.append(
            {
                "turn": turn,
                "act": act,
                "parse_error": perr or None,
                "awaiting_confirm": awaiting_confirm,
            }
        )
        if perr:
            print(f"-- reject: {perr}", flush=True)
            msgs.append({"role": "assistant", "content": raw})
            msgs.append(
                {
                    "role": "user",
                    "content": (
                        f"Rechazado: {perr}. "
                        + (
                            CONFIRM_USER
                            if awaiting_confirm
                            else (
                                'Ejemplo editar: {"action":"admin_v1","do":"editar","why":"…"}'
                            )
                        )
                    ),
                }
            )
            continue
        assert parsed is not None
        do = parsed["do"]

        if do == "editar":
            awaiting_confirm = True
            print("-- editar → ciclo de confirmación", flush=True)
            msgs.append({"role": "assistant", "content": raw})
            msgs.append({"role": "user", "content": CONFIRM_USER + f"\n## NOTEBOOK\n{nb}\n"})
            continue

        if do == "confirmar_editar":
            final = {**parsed, "confirmed": True}
            print("-- EXIT confirmar_editar", flush=True)
            print(f"   cubre={parsed.get('cubre', '')[:300]}", flush=True)
            print(f"   falta={parsed.get('falta', '')[:300]}", flush=True)
            break

        if do == "seguir_explorando":
            final = {**parsed, "confirmed": False, "wants_explore": True}
            print("-- EXIT seguir_explorando", flush=True)
            print(f"   brief={parsed['spawn']['brief'][:300]}", flush=True)
            break

        if do in ("cerrar", "ask_user"):
            final = parsed
            print(f"-- EXIT do={do}", flush=True)
            print(f"   why={parsed['why'][:300]}", flush=True)
            print(f"   reply={parsed.get('reply', '')[:500]}", flush=True)
            break

        print(f"-- spawn pedido tipo={parsed['spawn']['tipo']} (stub; pide salida)", flush=True)
        msgs.append({"role": "assistant", "content": raw})
        msgs.append(
            {
                "role": "user",
                "content": (
                    "En ESTA sonda el notebook del explore ya está fijado; no ejecutamos "
                    "más search/read/explore aquí. Elige cerrar (reply) o editar (gesto→confirmación), "
                    "o ask_user."
                ),
            }
        )

    summary = {
        "prompt": args.prompt,
        "explore_json": args.explore_json,
        "explore_veredicto": job.get("veredicto"),
        "model": args.model,
        "wall_sec": round(time.time() - t0, 1),
        "turns": turns,
        "exit": final,
        "awaiting_confirm_end": awaiting_confirm,
    }
    (out / "SUMMARY.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (out / "notebook_seed.md").write_text(nb, encoding="utf-8")
    print("==== DONE ====", flush=True)
    print(json.dumps(summary, ensure_ascii=False, indent=2)[:2500], flush=True)


if __name__ == "__main__":
    main()
