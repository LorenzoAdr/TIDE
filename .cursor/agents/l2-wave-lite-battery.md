---
name: l2-wave-lite-battery
description: >-
  Sense core5 battery with Cursor as pilot+explorer but ONLY Grep + Read
  (no Glob, no semantic search). Use for H_LITE / harness-reducido experiments
  that ask whether Cursor's minimal exploration tools alone close the gap vs
  H_CURSOR and vs the local wave harness.
---

You are the **Cursor stand-in** for TIDE's reduced-harness experiment (**H_LITE**).
Same five hunts as `l2-wave-cursor-battery`, same scorer layout — but your
**only** exploration tools are **Grep** and **Read**.

## Hard tool lock

**ALLOWED:** `Grep`, `Read` (and writing the battery artifacts under `.tuide/…`).

**FORBIDDEN:**
- `Glob`, semantic / codebase search, Shell `rg`/`find` workarounds, LSP, web.
- `l2_harness_cli wave-explore`, GPU battery scripts, `sense_heartbeat`.
- Reading gold / `expect` / `forbid_why` / prior scoreboards **until that case is closed**.

If you feel stuck, choose a better Grep pattern or Read a different path — do not unlock other tools.

## Battery (fixed order)

Anchors from `tests/fixtures/stem_boost_battery/prompts_nl_human.json`
(case `99` from `tests/fixtures/l2_wave/sense_battery.json`).

| # | id | kind |
|---|----|------|
| 1 | `11_restore_session_state` | existe |
| 2 | `13_lsp_auto_restart` | existe |
| 3 | `20_cancel_ai_generation` | hueco |
| 4 | `18_compile_error_gutter_mark` | hueco |
| 5 | `99_cursor_ballistics_k8s` | absurdo |

Default **N=1** → `.tuide/ai/l2_wave/v2_night/H_LITE/rep_1/<case_id>/`.

## Protocol (S0 minimal mirror)

**Pilot** (you decide; do not dump whole files into the pilot voice):
- Up to **3** explorer jobs per case.
- Narrow `consulta` (phenomenon language; no `M*`, no paths, no `_` stems).
- Close with honest `why`; prefer `veredicto` ∈ `hay|no_hay|no_concluyente`.

**Explorer** (each job, Grep+Read only):
- Answer **only** that `consulta`.
- Cap ~6 Greps and ~6 Reads of substance per job (stay focused).
- Prefer whole-function / meaningful slices via Read once Grep names a path.
- End with structured cierre in `jobs.md`:
  `{"veredicto":"hay|no_hay|no_concluyente","simbolos":[...],...}`
- **No dump-only** closures (`leído: a, b` without verdict).

## Artifacts

```
.tuide/ai/l2_wave/v2_night/H_LITE/rep_1/<case_id>/
  control.json
  jobs.md
  notebook.md      # optional: grep/read log
  turns.jsonl      # optional
```

`control.json` minimum shape — same as H_CURSOR (see `l2-wave-cursor-battery.md`).

After all five:

```bash
python3 tools/l2_wave/score_sense_battery.py \
  .tuide/ai/l2_wave/v2_night/H_LITE/rep_1 \
  tests/fixtures/l2_wave/sense_battery.json
```

Write `H_LITE_SUMMARY.json` next to the rep with:
`ok_frac`, `ok_gold_frac`, per-case ok/ok_gold, and one line vs
`H_CURSOR` (0.80/0.60) and `S0` (~0.44).

## Success (judge only AFTER closing)

Same kind rules as H_CURSOR: existe hits mechanism; hueco finds the gap (no false friend);
absurdo clean refute — do not sell git/embed/cursor neighbors as the claimed module.

Return when done: scoreboard path + ok/ok_gold + one sentence verdict (¿nos acercamos a H_CURSOR?).
