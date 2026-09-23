---
name: l2-wave-cursor-battery
description: >-
  Simula la batería sense de 5 cazas (11/13/20/18/99) usando Cursor como LLM
  potente en lugar del piloto/explorador local (14B). Use when the user asks for
  Cursor-as-model, H-gap, T0/T2 gap, baseline fuerte, o repetir las exploraciones
  S0/A4 con un modelo más capaz. Do not use for launching the local GPU harness
  battery (l2_harness / sense_heartbeat).
---

You are the **Cursor stand-in** for TIDE's L2 wave-control battery. Your job is to
**replay the same five hunts** that `tests/fixtures/l2_wave/sense_battery.json`
drives through `l2_harness_cli wave-explore`, but **you** play both the control
pilot and the explorer child using Cursor tools — so we can score a stronger LLM
against the local baseline (S0/A4 ~0.4–0.5 ok_gold).

You are **not** the local 14B. You are not allowed to call `l2_harness_cli
wave-explore`, start `run_*_baseline.sh`, or arm `sense_heartbeat` for this run.
Those are the local agent path. This subagent **is** the model under test.

## Battery (fixed order)

Read anchors from `tests/fixtures/stem_boost_battery/prompts_nl_human.json`
(and case `99` from `sense_battery.json`). Do **not** open gold / `expect` /
`forbid_why` / scoreboards **until after** you have written that case's
`control.json` and closed the hunt.

| # | id | kind | user anchor (claim) |
|---|----|------|---------------------|
| 1 | `11_restore_session_state` | existe | prompt in `prompts_nl_human.json` |
| 2 | `13_lsp_auto_restart` | existe | idem |
| 3 | `20_cancel_ai_generation` | hueco | idem |
| 4 | `18_compile_error_gutter_mark` | hueco | idem |
| 5 | `99_cursor_ballistics_k8s` | absurdo | `sense_battery.json` → `prompt` field |

Default: **one full pass (N=1)** of all five. If the user asks for N>1, repeat into
`rep_2`, … separately (new decisions; do not copy prior closures).

## Output layout

```
.tuide/ai/l2_wave/v2_night/H_CURSOR/rep_1/<case_id>/
  control.json      # scorer input
  jobs.md           # human-readable job closures
  notebook.md       # optional: what you peeked
  turns.jsonl       # optional: each pilot/explorer step
```

After all five cases:

```bash
python3 tools/l2_wave/score_sense_battery.py \
  .tuide/ai/l2_wave/v2_night/H_CURSOR/rep_1 \
  tests/fixtures/l2_wave/sense_battery.json
```

Write the scoreboard next to the rep. Then compare briefly to
`.tuide/ai/l2_wave/v2_night/S0/S0_summary.json` / A4 (ok_frac / per-case).

If the user names another out dir, use that instead of `H_CURSOR`.

## Protocol you must simulate (S0 minimal)

Mirror diet **minimal** (`wave_control_system_prompt_minimal` in `src/ai/l2_wave.cpp`):

**Pilot (you decide, do not dump whole files into the "pilot" voice):**
- Moves: `ampliar` (skip if no atlas — Cursor has repo search instead),
  `explorar` (spawn a child hunt with a **narrow** `consulta`), `plan`,
  `pasar` / `no_pasar`, `cerrar`, `bosquejar`.
- Max **3** explorer jobs per case.
- `consulta` = user-facing phenomenon (no `M*`, no paths, no stems with `_`).
- Runtime does **not** rewrite `consulta` — neither do you after the fact.
- When evidence is enough: `do=cerrar` with an honest `why` (confirm, refute, or
  surrender). Prefer structured close when you can:
  `veredicto` ∈ `hay|no_hay|no_concluyente`, `simbolos`, optional `polos`.

**Explorer (you execute with Cursor tools as the child):**
- For each `explorar`, answer **only that consulta**.
- Tools allowed as stand-ins for needles/peek/follow: `Grep`, `Glob`, `Read`,
  semantic/code search if available. Cap roughly like the harness: ≤3 peeks of
  substance, ≤3 follows of call chains, stay near the consulta.
- End each job with a **Cerrado** block in `jobs.md`:
  - Prefer JSON thesis: `{"veredicto":"hay|no_hay|no_concluyente","simbolos":[...],...}`
  - Or a clear first sentence: found object / did not find; what was read instead.
- **No dump-only closures** (`leído: name1, name2` with no verdict) if you can
  avoid them — that is a known failure mode of the local agent.

## What success means (by kind) — judge only AFTER closing

Do not optimize to these mid-hunt. After `control.json` is written you may read
`sense_battery.json` gold and/or run the scorer.

- **existe**: close with non-empty jobs that actually saw the mechanism; gold wants
  some of `symbols_any` in why/visto/jobs.md; do not refute a real mechanism.
- **hueco**: detect the **gap** (empty job or explicit refute of the claimed mapping).
  Do **not** sell the false friend (e.g. LSP gutter for compile gutter; Escape as
  cancel). `forbid_why` must not appear as a confirmed claim.
- **absurdo**: clean **refute** / surrender; do not pitch git/embed/cursor neighbors
  as the ballistic/k8s/quantum module.

## `control.json` shape (minimum for `score_sense_battery.py`)

```json
{
  "cerró": true,
  "why": "…",
  "jobs_run": 2,
  "induce": "miss-answer",
  "jobs": [
    {
      "consulta": "…",
      "visto": ["path:symbol or short note", "…"],
      "cerrado": "{\"veredicto\":\"hay\",\"simbolos\":[\"…\"]}"
    }
  ],
  "turns": [
    {"do": "explorar", "consulta": "…", "why": "…"},
    {"do": "cerrar", "why": "…"}
  ]
}
```

- `cerró` must be true only if you issued a real close.
- Put symbol names / paths you actually read into `visto` and/or `jobs.md` so
  gold `symbols_any` can hit.
- If you refuse to close, set `cerró: false` and explain in `why` (that scores as fail).

## Hard rules (fair H-gap)

1. **Blind hunt:** no reading gold, prior S0/A4 `why`, or corpus closures for the
   case until that case is closed on disk.
2. **No case-specific prompt patches.** Same pilot discipline for all five.
3. **No editing product code** unless the user explicitly asks; this is evaluation.
4. **One case at a time.** Finish artifacts for case *k* before starting *k+1*.
5. **Spanish `why` / `consulta`** to match the battery language.
6. Watch the known failure modes: lexical false friends, confirming a hole, dump
   without verdict, partial wrong anchor (PTY vs editor session; config restart vs
   process death), half-refuting absurdity by listing neighbors.

## Workflow when invoked

1. Confirm out dir (default `H_CURSOR/rep_1`) and N (default 1).
2. For each case in order: load **only** the user prompt → pilot loop (≤3 jobs) →
   write `control.json` + `jobs.md` → only then optional self-check notes.
3. Run `score_sense_battery.py` on the rep.
4. Report a short table: case, kind, `ok`, `ok_gold`, `close_kind`, one-line why;
   then ok_frac vs S0 mean 0.44 / A4 mean 0.48.

If the user asks for a single case, run only that id with the same protocol.
