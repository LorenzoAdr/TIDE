# Plan: Effect Summary en Fase A (fichas AST → olfateo → expansión)

**Estado:** diseño normativo (sin implementar en este documento)  
**Fecha:** 2026-08-22  
**Origen:** propuesta de migrar peeks de cuerpo → fichas estructuradas + discusión sobre tamaño de cuerpos en A  
**Prerrequisito:** [`l2-explore-phase-a-b.md`](l2-explore-phase-a-b.md) (P0–P7 implementado; trail + dataflow ya en árbol)  
**Flag propuesto:** `L2_EXPLORE_EFFECT_SUMMARY` (off por defecto hasta P6 verde)

---

## 0. Opinión sobre la propuesta (veredicto)

La dirección es **correcta y alineada** con el problema real de A: el 7B juzga mal cuando el prompt mezcla cuerpos largos, trails y caza libre. Sustituir la **unidad de presentación** del olfateo (cuerpo) por una **ficha AST corta** multiplica el throughput de candidatos sin saturar `n_ctx`.

Pero la propuesta, tal cual, tiene tres puntos frágiles que hay que corregir antes de implementarla:

1. **Las fichas engañan igual que las firmas.** El plan A/B ya documenta: *“A mira cuerpos (firmas engañan)”*. Un Effect Summary **no puede coronar `useful` → locus**. Solo puede votar triage (`EXPAND` / `REJECT` / `UNCERTAIN`). El locus durable exige **peek confirmatorio** (o trail/dataflow + peek) antes de `a_done`.
2. **20–25 fichas/vuelta es alto para Qwen2.5-Coder-7B.** Con 5–10 líneas × 25 ≈ 125–250 líneas de señal densa + Instruction + notes. Mejor default: **12–16 fichas/vuelta** (p95 ≤ 20), y subir solo si battery lo justifica.
3. **No inventar un segundo pipeline paralelo.** Ya existen cola rankeada, embed, `a_judge`, trail (call-path TS) y dataflow (rg). Effect Summary debe ser una **capa de presentación + rerank** encima de A, no un explorador nuevo. Vocabulario: mapear a veredictos existentes; no renombrar el contrato JSON sin necesidad.

Con esas correcciones, la migración vale la pena: más candidatos tocados, menos tokens basura, y las herramientas caras (peek / trail / dataflow) solo para los que sobreviven al olfateo.

---

## 1. Problema que resuelve

### 1.1 Hoy (Phase A promovida)

| Paso | Qué pasa |
|------|----------|
| Cola | Top ~40 ventanas (léxico + stem/body embed + diversify) |
| Tranche | **5 peeks de cuerpo** (~40–80 líneas c/u) inyectados por runtime |
| Juicio | `useful` / `reject` / `uncertain` / `interesting` |
| Profundización | Trail call-path + dataflow rg-only (ya cableados) |
| Cierre | `loci[]` → Phase B pack |

Cuello de botella: **cada juicio consume cuerpo**. Funciones de 200–400 líneas se parten en `#tail`/`#head`, pero igual:

- Se gastan peeks en símbolos que un outline estructural habría descartado.
- El 7B ve poco contraste (5 ítems) y cierra mal o pide trail demasiado pronto.
- Mezclar cuerpo + trail en el mismo prompt (si se descuidara el runtime) satura y confunde — la regla “nunca mezclar” debe ser **hard** en el scheduler.

### 1.2 Objetivo

Separar **triage barato** (fichas) de **confirmación cara** (peek / trail / dataflow):

```text
Instruction
    │
    ▼
L1 / seeds (intención + needles)     ← reutilizar, no duplicar llamada 7B si ya hay shortlist
    │
    ▼
Cola estructural top 40–80           ← igual que hoy + hotspots
    │
    ▼
Effect Summary (TS, 5–10 líneas)     ← NUEVO: unidad de presentación
    │
    ▼
Rerank embed de fichas vs intención  ← NUEVO (opcional flag embed)
    │
    ▼
Olfateo A0: 12–16 fichas → EXPAND|REJECT|UNCERTAIN
    │
    ▼
Expansión A1 (una modalidad / turno):
    • peek 40–60 líneas  XOR
    • trail (call hierarchy local)  XOR
    • dataflow tree
    │
    ▼
a_done → loci[] → Phase B (sin cambio de contrato)
```

Principio endurecido: **Embed propone, ficha tria, peek/trail/dataflow confirma, pack materializa.**

---

## 2. Definición normativa: Effect Summary

### 2.1 Qué es / qué no es

| Es | No es |
|----|-------|
| Extracción **determinista** del AST (Tree-sitter) + heurísticas locales | Resumen generado por el 7B |
| 5–10 líneas de texto estable, cacheable por hash de cuerpo | Dump de cuerpo ni outline completo del archivo |
| Señal para **triage** | Evidencia suficiente para editar o para `useful` final |
| Indexable / embeddable como passage corto | Sustituto de `pack.md` |

### 2.2 Esquema de ficha (texto canónico)

Formato fijo (markdown mínimo, fácil de parsear y de embeber):

```text
# ES path/to/file.cpp:Symbol
sig:    RetType Qual::Symbol(args…)
role:   mutator | query | glue | io | lock | ui | parse | unknown
calls:  foo, bar, baz                    # ≤8, orden AST / frecuencia
writes: m_flag, g_state                   # LHS / fields / globals tocados
reads:  cfg.enabled, argc                 # idents relevantes no-write
ctrl:   early-return×3; if×2; lock×1      # conteos + flags estructurales
hot:    early_return, throws, atomic, sleep, wake, file_io, network
anchor: path:Symbol  lines:120-310  window_hint:tail
why_hint: "setea flag y return; no valida X"   # opcional, 1 línea heurística
```

**Presupuesto:** ≤ **10 líneas** / ≤ **700 caracteres** por ficha. Si hay overflow: truncar `calls`/`reads` primero; nunca truncar `sig` + `hot` + `anchor`.

### 2.3 Extracción Tree-sitter (runtime)

Por símbolo functionish:

| Campo | Fuente |
|-------|--------|
| `sig` | Primera línea / declarator (reutilizar lógica de `a_trail_enrich_hop` / `function_signature_line`) |
| `calls` | `call_expression` en el cuerpo; filtrar ruido (`std::`, getters triviales opcionales) |
| `writes` / `reads` | Asignaciones / idents en LHS vs RHS (heurística C++/TS existente + endurecer) |
| `ctrl` | Conteos de `if`/`return`/`throw`/`for`/`while`/`try` + early-return (return antes del final) |
| `hot` | Matchers estructurales (lista cerrada, §2.4) |
| `role` | Reglas: si `writes`+early-return → mutator; si solo reads/calls query → query; si hop UI/wake → ui; etc. |
| `why_hint` | Plantilla local (no LLM): p.ej. `"early-return si !flag; escribe m_x"` |

**Idiomas:** C/C++ primero (camino caliente TUIDE); Python/TS/Go cuando el extractor de símbolos ya existe. Fallback si TS falla: ficha mínima `{sig=primera línea, role=unknown, hot=[], anchor}` — **nunca** caer a cuerpo completo en A0.

### 2.4 Hotspots estructurales (lista cerrada v1)

Flags booleanos / tags en `hot:` (extensibles vía config, no ad-hoc del modelo):

- `early_return`, `multi_return`, `throws`, `catch`
- `lock`, `atomic`, `sleep`, `timeout`
- `file_io`, `network`, `subprocess`
- `wake`, `ui_event`, `spinner` (patrones TUIDE / idents conocidos)
- `global_write`, `static_mut`
- `todo_fixme`, `assert_fail`

Sirven para **boost de cola** (capa ranking) y para que el 7B vea “por qué está aquí” sin leer el cuerpo.

---

## 3. Flujo detallado (pasos 1–5 mejorados)

### Paso 1 — Intención + seeds (una llamada, si hace falta)

| Propuesta original | Ajuste |
|--------------------|--------|
| “El 7B extrae intención + seeds del prompt (1 llamada)” | **Reutilizar L1** (`coding_stem_shortlist`, needles/facets, `context_stem` hint). Solo llamar al 7B si no hay shortlist L1 o si A arranca standalone. |

Salida durable (en `a_state.json`):

```json
{
  "intent": "por qué el spinner no cancela al abortar",
  "seeds": ["spinner", "cancel", "abort", "wake"],
  "facets": ["ui", "ai_controller"]
}
```

No meter el prompt de usuario crudo en cada olfateo: solo `intent` compacto + orphans.

### Paso 2 — Filtro candidatos (ranking + hotspots)

Igual que cola actual (`build_a_scan_queue`) con **boost**:

```text
score' = lexical + stem_embed + body_embed_hint
       + w_hot * hotspot_overlap(seeds, hot)
       + w_orphan * orphan_overlap
```

Top **40** en cola activa; **41–80** en `reserve` (expansión P3 existente). Diversify por stem/path family se mantiene.

### Paso 3 — Embeddings online de Effect Summaries

| Rol | Detalle |
|-----|---------|
| Passage | Texto canónico de la ficha (§2.2), **no** el cuerpo |
| Query | `intent` + seeds |
| Acción | Reordenar la tranche visible / priorizar reserve |
| Prohibido | Vectores en el prompt; passages “ricos” tipo cuerpo (battery: baseline > rich_480) |

Cache: `.tuide/ai/l2/effect_summary_cache/` keyed por `path + symbol + content_hash`. Invalidar con mtime/hash del archivo.

Si embed off: orden = score' del paso 2.

### Paso 4 — Olfateo A0 (fichas)

**Entrada al 7B:**

- Instruction compacta / intent
- `a_notes` (solo veredictos + mini-why; sin cuerpos)
- **12–16 fichas** (p95 20) de la cola
- Metadatos: `cursor 16/40`, orphans, stems reject

**Salida (extender `a_judge`, no inventar action nueva si se puede evitar):**

```json
{
  "action": "a_judge",
  "phase": "a0_sniff",
  "verdicts": [
    {
      "target": "src/ui/wake_policy.cpp:should_wake",
      "verdict": "expand",
      "expand_with": "peek",
      "why": "hot early_return + write m_armed; cuadra con cancel"
    },
    {
      "target": "src/ui/wake.cpp:tick",
      "verdict": "reject",
      "why": "glue de despacho; sin writes ni cancel"
    },
    {
      "target": "src/ai/controller.cpp:on_abort",
      "verdict": "uncertain",
      "expand_with": "dataflow",
      "suspect_var": "m_thinking",
      "why": "tocá flag; falta ver quién lo limpia"
    }
  ],
  "done": false
}
```

Mapeo a tipos existentes:

| Veredicto A0 | Semántica | Efecto runtime |
|--------------|-----------|----------------|
| `expand` | Merece herramienta cara | Encola modalidad (§5); **no** corona locus |
| `reject` | Fuera de vecindario | Anota; puede demote stem |
| `uncertain` | Ficha insuficiente | Default: 1 peek corto **o** pasar; no gastar trail |
| (`useful` prohibido en A0) | — | Runtime rechaza / downgrade a `expand`+peek |

Alias aceptados en parse: `EXPAND`→`expand`, etc. (case-insensitive).

**Presupuesto A0:**

| Recurso | Tope |
|---------|------|
| Fichas / vuelta | 12–16 (p95 20) |
| Vueltas A0 | ≤ 4 |
| Fichas totales olfateadas | ≤ 64 |
| `expand` por vuelta | ≤ 4 |
| `expand` totales antes de forzar A1/cierre | ≤ 12 |

### Paso 5 — Expansión A1 (modalidades exclusivas)

El modelo **elige modalidad**; el runtime **ejecuta y presenta un solo tipo de evidencia** por turno.

| `expand_with` | Cuándo | Qué ve el 7B | Reutiliza |
|---------------|--------|--------------|-----------|
| `peek` | Confirmar zona / early-return / mutación | 40–60 líneas (`#tail` si largo) | `get_code_of` ventanas |
| `trail` | Sospecha secuencia / arquitectura / callers | Stacks call-path enriquecidos (sin cuerpos de callee) | `a_trail_*` |
| `dataflow` | Variable/campo crítico | Árbol decl/write/read | `a_dataflow_*` |

**Reglas hard del scheduler (aceptación):**

1. **Nunca** mezclar trail + cuerpo en el mismo prompt.
2. **Nunca** mezclar dataflow + trail en el mismo prompt.
3. **Nunca** inyectar función completa > ~80 líneas; ventanas acotadas.
4. Tras evidencia A1: veredictos clásicos `useful` / `reject` / `uncertain` / `interesting` (contrato actual).
5. Solo `useful` (post-A1) puede alimentar `loci_draft` / trail begin.
6. Si A0 dijo `expand`+`peek` y el peek confirma → `useful` + ancla; si no → `reject` o seguir cola.

Escape hatch (máx. 1–2 / run): sibling header del mismo stem tras `useful` — igual que plan A/B.

---

## 4. Relación con Phase A actual (no romper)

| Componente actual | Con Effect Summary |
|-------------------|--------------------|
| `build_a_scan_queue` | Sigue; ítems apuntan a símbolo; ficha se materializa lazy |
| `a_judge` / `a_done` | Extender con `phase=a0_sniff` y `expand_with`; `a_done` igual |
| Trail / dataflow | Solo vía A1, no en tranche A0 |
| Presupuestos peeks 24–32 | Se convierten en **presupuesto mixto**: fichas baratas + peeks caros (p.ej. ≤ 16 peeks + ≤ 64 fichas) |
| Phase B / pack | **Sin cambio** — re-fetch anclas; no confiar en ficha ni en why de A0 |
| Flag `L2_EXPLORE_PHASE_A` | Sigue siendo el padre; ES es sub-flag |

Diagrama de estados:

```text
explore_a
  ├─ a0_sniff (fichas) ──expand──► a1_peek | a1_trail | a1_dataflow
  │                      reject ──► next tranche
  │                      uncertain ► peek ligero o skip
  └─ a_done(loci) ──► explore_b
```

---

## 5. Mejoras concretas a la propuesta original

| # | Original | Mejora |
|---|----------|--------|
| 1 | Unidad = ficha; olfateo vota EXPAND/REJECT/UNCERTAIN | OK; **prohibir `useful` en A0**; useful solo post-peek/trail/dataflow |
| 2 | 20–25 fichas/vuelta | Bajar a **12–16**; medir y subir |
| 3 | 1 llamada 7B para intención | Reusar L1; llamada extra solo si falta shortlist |
| 4 | Embeddings de summaries | Sí, passages = ficha canónica; cache por hash; no rich body |
| 5 | Call hierarchy / dataflow / peek a demanda | Ya existen; formalizar **mutex de modalidad** en runtime |
| 6 | “Nunca cuerpo de 300 líneas” | Ya parcialmente; ES lo hace default en A0; A1 peek ≤ 60 |
| 7 | Firma compleja | Empezar por extractor **heurístico TS** determinista; no ML summary |
| 8 | — (faltaba) | Sesgo anti-falso-reject: score alto + hotspot overlap **no** se `reject` en A0 sin al menos `uncertain`+peek |
| 9 | — (faltaba) | Telemetría: `A0_cards`, `A0_expand_rate`, `A1_modality_mix`, `false_reject_gold` |
| 10 | — (faltaba) | Fallback degradado: si extractor falla → ficha mínima, no cuerpo |

### 5.1 Sesgo de seguridad (falso reject)

Regla runtime:

- Si `score` en top-15 **o** `hot ∩ seeds ≠ ∅` → `reject` del modelo se **downgrade** a `uncertain` la primera vez; obliga peek o segunda ficha con otra ventana hint.
- Gold battery: métrica `summary_false_reject` debe tender a 0 en casos known.

### 5.2 Qué no hacer

- No generar Effect Summaries con el 7B (caro + alucina side effects).
- No embeber cuerpos “para que el embed entienda mejor”.
- No abrir A0 a tools libres del modelo.
- No usar ficha como fragmento de pack en B.
- No reintroducir LSP call hierarchy en hot path (seguir índice local / trail TS).

---

## 6. Plan de implementación (ingeniería)

Orden estricto; sin estimación de calendario. Cada fase con criterio de salida comprobable.

### P0 — Contrato y flag

**Trabajo:**

- Flag `L2_EXPLORE_EFFECT_SUMMARY` (default off; requiere `L2_EXPLORE_PHASE_A`).
- Tipos: `EffectSummary`, `A0Verdict` (`expand`/`reject`/`uncertain`), `expand_with` enum.
- Extender parse `a_judge` con `phase` + `expand_with` + `suspect_var`.
- Doc: este plan + nota en `l2-autonomous.md`.

**Salida:** compila; flag off ≡ comportamiento actual bit-a-bit.

### P1 — Extractor TS → Effect Summary

**Trabajo:**

- `effect_summary_build(abs_path, symbol)` → texto canónico + JSON.
- Reusar parsers/símbolos existentes; tests con fixtures C++ (early-return, lock, glue puro).
- Cache en disco; invalidación por hash.
- Fallback ficha mínima.

**Salida:** test unitario: dado `should_wake`-like fixture, ficha contiene `early_return` + writes esperados; ≤ 10 líneas.

### P2 — Cola + hotspots + embed de fichas

**Trabajo:**

- Boost `score'` con overlap hot/seeds.
- Rerank opcional de tranche por cosine(intent, ficha).
- Builder de tranche A0: 12–16 fichas, diversify.

**Salida:** fixture determinista; gold con hotspot sube al menos N posiciones vs baseline sin boost.

### P3 — Loop A0 (olfateo)

**Trabajo:**

- Subfase `a0_sniff` en session loop: inyectar fichas (no `get_code_of`).
- Aplicar veredictos; downgrade `useful`→`expand`; anti-false-reject (§5.1).
- Compactar: notes sin cuerpos; contar `A0_cards`.
- Early-stop A0 → pasar a A1 pendientes o `a_done` solo si ya hay loci post-A1.

**Salida:** harness: A0 barre 40 candidatos en ≤ 4 vueltas sin escribir pack ni peeks (salvo uncertain forzado).

### P4 — Scheduler A1 (modalidad exclusiva)

**Trabajo:**

- Cola de expansiones `{target, modality, suspect_var?}`.
- Un prompt = una modalidad; mutex hard + assert en tests.
- Tras A1: veredictos clásicos; `a_trail_begin` solo desde `useful`.
- Presupuesto peeks ≤ 16; trails ≤ 4 inicios; dataflow ≤ 4 reportes.

**Salida:** test: intento de mezclar trail+peek en un turno → runtime separa o rechaza; battery no-LSP sigue verde.

### P5 — Cableado a `a_done` / B / expansión de cola

**Trabajo:**

- Orphans y capas 1–3 operan sobre cola de símbolos (fichas lazy).
- Micro-A desde B: puede entrar en A0 (fichas) o A1 peek directo si path allowlisted.
- Brief a B: mini-why de A1, **no** volcar todas las fichas.

**Salida:** criterios globales §8 con flag on; Phase B sin regresión de contrato.

### P6 — Eval / batteries

**Trabajo:**

- Métricas nuevas en `score_explore.py` / `PHASE_A_METRICS.md`:
  - `A0_cards`, `A0_turns`, `A1_peeks`, `A1_trails`, `A1_dataflows`
  - `summary_false_reject`, `modality_violation` (=0)
  - `premature_useful_on_card` (=0)
- Casos: long-function (ficha vs peek), glue-reject, hotspot-boost, multi-stem spinner, rank-miss 45–70.
- Comparar: Phase A body-peek vs ES (calidad loci / ready_to_edit, no solo tokens).

**Salida:** informe; criterio de promoción.

### P7 — Promoción

**Trabajo:**

- Prompt packs / harness: instrucciones A0 vs A1.
- Default on solo tras P6; rollback `L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=0`.

**Salida:** feature promovida o documentada como experimental con gate explícito.

---

## 7. Archivos previstos (orientativo)

| Área | Archivos |
|------|----------|
| Extractor | `src/ai/l2_effect_summary.*` (nuevo), hooks en `l2_explore_a_trail.cpp` / parser TS |
| Estado / loop | `l2_explore_a.*`, `level2_session.*`, `level2_autonomous_loop.*`, `l2_action*` |
| Embed | `coding_embed_rerank.*` (passage = ficha) |
| Flags | `features_promoted.json` |
| Docs | este plan, `l2-autonomous.md`, `l2-explore-no-lsp.md` |
| Eval | `tools/l2_explore_battery/*`, fixtures long-body / hotspot |

---

## 8. Criterios de aceptación

1. Flag off: cero cambio observable vs Phase A actual.
2. En A0 nunca se inyecta cuerpo de función (solo fichas o ficha mínima).
3. `useful` en respuesta A0 → downgrade; telemetría `premature_useful_on_card == 0`.
4. Un turno A1 muestra **una** modalidad; `modality_violation == 0`.
5. Ningún prompt A1 con trail + body juntos.
6. Peek A1 ≤ 60 líneas (salvo símbolo más corto).
7. p95: A0_turns ≤ 4; A1_peeks ≤ 16; peeks+fichas dentro de budget local 8k.
8. Battery long-tail: gold en cola de función >200 líneas localizado sin dump de 300 líneas.
9. `summary_false_reject` en casos gold top-15 → 0 tras anti-reject.
10. Explore no-LSP sigue completo (P5 del plan A/B).
11. Phase B: pack solo desde loci post-confirmación; re-fetch obligatorio.

---

## 9. Riesgos y mitigaciones

| Riesgo | Mitigación |
|--------|------------|
| Ficha omite side effect crítico | Anti-false-reject; uncertain→peek; useful solo post-A1 |
| Extractor frágil en macros C++ | Ficha mínima + peek; no bloquear cola |
| 7B marca todo EXPAND | Cap 4 expand/vuelta; sobrantes → uncertain |
| 7B marca todo REJECT | Downgrade top-score; battery false_reject |
| Coste de build de fichas en repos grandes | Lazy + cache hash; solo top-80 |
| Duplicar intención L1/L2 | Reusar shortlist; una sola estructura `intent` en state |
| Regresión single-stem fácil | Comparar flag off/on; rollback ES |

---

## 10. Fuera de alcance

- Cambiar Phase B / edit / compile.
- Resumen LLM de funciones (offline o online).
- Call hierarchy LSP.
- RAG vectorial global (Fase F master-spec).
- Sustituir trail/dataflow existentes (solo reordenarlos detrás de A0).

---

## 11. Checklist de handoff

- [ ] Aprobar esquema de ficha (§2.2) y presupuestos A0/A1
- [ ] Aprobar mutex de modalidades y prohibición de `useful` en A0
- [ ] P0 flag + schemas
- [ ] P1 extractor + tests
- [ ] P2 hotspots + embed fichas
- [ ] P3 loop A0
- [ ] P4 scheduler A1
- [ ] P5 B / expansión
- [ ] P6 batteries + informe
- [ ] P7 promoción condicionada

---

## 12. Mapa propuesta chat ↔ este plan

| Idea del chat | Dónde queda |
|---------------|-------------|
| Unidad = ficha 5–10 líneas TS | §2 |
| L1 intención + seeds | §3 paso 1 (reusar L1) |
| Ranking + hotspots top 40–80 | §2.4, §3 paso 2 |
| Embed summaries vs intención | §3 paso 3 |
| 20–25 fichas → EXPAND/REJECT/UNCERTAIN | §3 paso 4 (**12–16**; sin useful) |
| Expansión trail / dataflow / peek | §3 paso 5 + código trail/dataflow existente |
| No mezclar hierarchy + cuerpo | §3 paso 5 reglas hard; §8.4–5 |
| No dar 300 líneas de golpe | §3 paso 5; peek ≤ 60 |

---

## 13. Referencias

- [`l2-explore-phase-a-b.md`](l2-explore-phase-a-b.md) — Phase A/B base
- [`../ai/l2-explore-no-lsp.md`](../ai/l2-explore-no-lsp.md) — locate sin LSP
- `src/ai/l2_explore_a.hpp` — `AState`, trail, dataflow
- `src/ai/l2_explore_a_trail.cpp` — hops / signatures TS
- `src/ai/l2_explore_a_dataflow.cpp` — decl/write/read
- `tools/l2_explore_battery/PHASE_A_METRICS.md` — gates actuales
