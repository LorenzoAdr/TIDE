# Rúbrica supervisada — ProblemFrame L1 (genérica)

Sin stems de producto. Mejoras solo en: prompt L1, sanitize/grounding léxico, schema.

## Fallos observados (baseline `core5_pf_generic`)

1. Inventa toolchains ajenos (`gradle`, `npm`, `yarn`) no dichos en el prompt.
2. Primary mezcla **contexto ambiente** (chat, IA) con el **objeto focal** (spinner/carga, cancel, build).
3. Terms en español glosados a inglés sin anclaje léxico (`loading_state` sin “loading” en query).

## Criterios de calidad (genéricos)

| Check | OK | Mal |
|-------|----|-----|
| Grounding | cada `search_term` comparte stem ≥4 chars con algún token del query | términos huérfanos (npm, gradle, ai_model…) |
| Primary focal | nombra el objeto a localizar/cambiar | ambient (app, chat, asistente) como primary |
| Secondary | ambiente / orquestación / callers | vacío cuando hay contexto claro |
| Sin frases NL | snake_case / CamelCase | `"loading state"` |

## Bucle

```
RUN core5 L1 → score estructural + grounded_ratio → agente propone ≤2 cambios genéricos → re-run
```

Prohibido: listas de stems de este repo, `if case_id`, augments por dominio (AI/settings/…).

## Iteración 1 (código)

- Prompt L1: primary = objeto focal; terms = proyección léxica del query; no inventar toolchains.
- `problem_frame_refine_from_query`: drop terms sin stem ≥4 compartido con el query (fold acentos).
- Score: `grounded_ratio` + `ungrounded_terms`.

## Entityness (post-ProblemFrame, match_surface)

**No** se calcula sobre el prompt crudo. Flujo:

```text
L1 → problem_frame (primary + secondary = eslabones, prompt-grounded)
  → [si confidence low|medium] hipótesis de ancla acotadas a menú índice/outline
  → entityness_score_problem_frame(links = primary + secondary + hyp_*)
  → explore_mode = f1_anchor | classic_scan
  → si gana hyp_N: active_hypothesis_index = N (seeds / F1 filter)
```

- Entityness alta en un eslabón o hipótesis → caza F1 (ancla).
- Todo difuso → `classic_scan` (mapa/cola sin filtro F1).

### Hipótesis (pasada B)

Cuando el PF grounded en el prompt es débil, L1 propone 2..4 `anchor_hypotheses` eligiendo
`search_terms` de un **menú corto** (top stems/símbolos del índice + outline). Grounding de
hyps = menú (no query). Prohibido inventar fuera del menú. Entityness confirma o descarta.

```bash
# Tras una ronda L1 con problem_frame.json + map:
python3 tools/l2_core5_battery.py entityness --label entity_links_v1 \
  --from-round .tuide/ai/l2_explore_battery/round_core5_pf_v3
```

CLI: `l2_harness_cli entityness-probe --problem-frame-json PATH [--out F]`

Review humana (sin stems hardcodeados en scorer):

| Caso | Expectativa |
|------|-------------|
| 17 | eslabón con `spinner` más entidad que ambient; mode según umbral |
| 07 | compile/build |
| resto | primary del PF vs secondaries vs hyp_* |

Gate: JSON `links[]`, `explore_mode` ∈ {f1_anchor, classic_scan}, scores ∈ [0,1],
`best_role` ∈ {primary, secondary_N, hyp_N}.
