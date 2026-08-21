# Plan: L2 explore en dos fases (A localización / B pack)

**Estado:** diseño normativo (sin implementar en este documento)  
**Fecha:** 2026-08-21 (rev. chat + PR #10)  
**Origen:** [PR #10](https://github.com/LorenzoAdr/TIDE/pull/10) + discusión de diseño 2026-08-20/21  
**Alcance:** plan detallado para separar **identificación de locus/stem** de **acumulación de código** en L2 explore; no cambia runtime por sí solo.

---

## 0. Resumen ejecutivo

Hoy L2 explore **mezcla** dos trabajos que confunden al modelo (sobre todo 7B):

1. **Identificar** dónde puede estar el comportamiento (stem / locus / vecindario).
2. **Acumular** fragmentos en `pack.md` hasta `done next=edit`.

El síntoma típico (battery `17_ai_spinner_stuck`, round `anchor_rescue_v1`): el primer `action=plan` ya mete 4–5 stems distintos; `PACK_REVIEW` dice `partial`; el runtime responde con más plans / pushbacks / auto-plan a módulos peores. El sistema trata un fallo de **identidad de locus** como si fuera un fallo de **cobertura de pack**.

La alternativa es un explorador en **dos fases explícitas**:

| Fase | Job | Pregunta | Memoria durable | Cierre |
|------|-----|----------|-----------------|--------|
| **A — Localización** | Decidir **loci** críticos | ¿Qué stem(s)/anclas son el vecindario correcto? | stem + ancla + mini-resumo (`a_notes`) | hipótesis estable / early-stop |
| **B — Acumulación** | Cubrir loci con pack rígido | ¿Qué código hace falta para editar/responder? | fragmentos (`pack.md`) | `pack_incomplete` falso + `PACK_REVIEW` OK → `done next=edit` |

Principios:

- **Robustez > ahorro de ciclos** L2.
- A **mira** cuerpos (firmas engañan) pero no los **guarda** como pack.
- **Embed propone, peek dispone, pack materializa.**
- A es **escaneo estricto** sobre cola rankeada (índice estructural local), no navegación libre del modelo.
- Miss del top-N = **expansión de cola**, no explore libre.
- Sustituir dependencia del **LSP server** en el hot path de localización por **índice estructural local** (Tree-sitter / `SymbolIndex` + mapa). Call hierarchy LSP **no** es requisito; si hace falta “profundizar”, se hace con defs/refs/outlines/siblings del índice local una vez hay stem locked.
- L1 ya intuye la separación (`coding_stem_shortlist`, `context_stem`, `pick_stem`); L2 explore debe **usarla como fase**, no saltar a empaquetar.

La propuesta **no tira** el cableado actual (mapa L1, stem embed, body semantic rerank, `plan`→pack, `PACK_REVIEW`, diversidad por path): lo **reparte** en A vs B, hace explícito el miss de ranking, y ancla la localización al índice local.

---

## 1. Problema y motivación

### 1.1 Qué falla hoy

- Explore permite tools + `action=plan` + crecimiento de Observations **y** pack a la vez.
- El modelo compite entre “seguir cazando” y “ya tengo bastante para editar”.
- Lock prematuro a un solo `context_stem` (L1 enrich) empuja bugs **multi-stem** al módulo equivocado; a la inversa, **no** lockear y planear multi-stem prematuro ensucia el pack.
- `PACK_REVIEW` pregunta “¿el pack sirve para editar?” cuando a menudo la pregunta correcta era “¿estamos en el módulo correcto?”.
- Depender de LSP (hover, call hierarchy, workspace symbols) para localizar es frágil: clangd down, `compile_commands` incompleto, indexado a medias.

### 1.2 Insight del chat (por qué mezclar confunde)

Son **políticas distintas**:

| | Identificar (A) | Acumular (B) |
|--|-----------------|--------------|
| Descartar | bueno y barato | caro (ya invertiste tokens) |
| Evidencia | outline / 1 ancla / peek corto | fragmentos + siblings + re-fetch |
| Gate | “¿este stem es el vecindario?” | “¿cubre Instruction para editar?” |
| Agencia del modelo | juzgar cola que manda el runtime | plan/pack acotado a loci |

Mezclarlas hace que el modelo “vote con código”: mete targets por incertidumbre → pack ruidoso → review pide más → contexto saturado con evidencia de stems equivocados.

### 1.3 Qué no es el problema

- No es solo “el pack es pequeño”.
- No es solo “el 7B es tonto”: el 7B **puede** juzgar bien ~60 líneas × 5 peeks; falla cuando el contexto mezcla peeks viejos + acumulación + caza libre.

### 1.4 Objetivo de producto

Explore termina con un **set de loci** justificado y un **pack** que los cubre, sin que el brain retenga basura de la caza. Ciclos L2 pueden subir; la señal por propose debe bajar en ruido.

---

## 2. Definiciones

### 2.1 Locus

Unidad durable de A (no un fragmento de pack):

```text
stem:     basename del módulo (settings_modal, ui_wake_policy, …)
anchor:   path:Symbol | path:line | path:A-B
role:     primary | secondary | suspect
why:      1–2 líneas (mini-resumo)
window:   opcional — head | tail | mid | hit  (para cuerpos largos)
```

### 2.2 Peek vs keep

| | Peek (A) | Keep (B) |
|---|----------|----------|
| Qué es | Cuerpo/ventana efímera para juzgar | Fragmento en `pack.md` |
| Vida | 1 vuelta (luego compactar/tirar) | Hasta edit / compactación de budget |
| Criterio | useful / reject / uncertain | roles, diversity, instruction gap, review |

### 2.3 Cola de A

Lista ordenada de **ventanas candidatas** (no necesariamente 1:1 con símbolos):

- Origen: top-K del mapa fusionado (léxico + stem/body embed).
- Cada ítem: `path`, símbolo o línea, hint de ventana (`#tail` si body largo), score, `stem`.
- El runtime avanza la cola; A solo emite veredictos.

### 2.4 Stem candidate vs locus

| Concepto | Fase | Qué es |
|----------|------|--------|
| **Stem candidate** | A (entrada de cola) | Módulo propuesto por ranking; aún no confirmado |
| **Locus** | A (salida) | Stem + ancla + role + why confirmados |
| **Pack fragment** | B | Cuerpo materializado en `pack.md` para editar |

El bucle mental del chat (“dime stems → itera descartando → cuando hay stem interesante pide código”) se implementa así: la **cola rankeada** propone stems/ventanas; A **descarta o confirma**; B **pide código sabiendo a dónde va**.

---

## 3. Arquitectura objetivo

```text
Instruction / query
        │
        ▼
┌───────────────────┐
│ Índice estructural│  SymbolIndex (TS) + RepoMap + stem/body embed
│ local (sin LSP)   │  shortlist top-K rankeada
└─────────┬─────────┘
          │ cola fija + expansiones acotadas
          ▼
┌───────────────────┐
│ Fase A            │  escaneo 5 ventanas/vuelta
│ peeks efímeros    │  notas: useful/reject + mini-resumo
│ early-stop        │  salida: loci[]  (1–N stems)
└─────────┬─────────┘
          │ loci estables (+ brief)
          ▼
┌───────────────────┐
│ Fase B            │  plan → apply_plan → pack.md
│ pack rígido       │  pack_incomplete + PACK_REVIEW
│ re-fetch anclas   │  profundiza por stem locked (outline/
│                   │  siblings/defs-refs locales)
│                   │  done next=edit
└─────────┬─────────┘
          ▼
        edit / compile  (sin cambio de contrato)
```

### 3.1 Índice estructural local (sustituto de LSP en A/B locate)

**En alcance para localización y pack:**

- Tree-sitter defs/refs + `SymbolIndexSnapshot`
- `file_outline` / `get_code_of` vía TS (ya existente)
- `RepoMap` + facet coverage + `CodingStemEmbedIndex` + `BODY_SEMANTIC_RERANK`
- headers/siblings por path/stem (FS + índice)

**Fuera del camino crítico de A** (opcional / degradable):

- clangd workspace symbols, hover, incoming/outgoing calls (LSP call hierarchy)
- Cualquier tool que falle si el language server no está ready

**Reconciliación con la intuición “call hierarchy tras encontrar el stem”:**  
Sí a la **idea** (una vez hay stem, profundizar el grafo de llamadas/usos). No al **medio LSP** como requisito. En B, “profundizar” = outline del stem + refs/defs del índice local + siblings `.hpp` + `get_code_of` anclado. Si más adelante hay call-graph local (TS), se enchufa ahí; no se bloquea A/B a clangd.

Criterio de robustez: **un workspace indexado por TS debe poder completar A→B→ready_to_edit** aunque LSP esté down. Diagnostics LSP pueden seguir existiendo para edit/compile feedback, pero no bloquean localización.

---

## 4. Fase A — Localización (identificar)

### 4.1 Contrato del modelo (por vuelta)

Entrada (runtime):

- Instruction / query (compacta)
- `A_notes` acumuladas (solo mini-resúmenes + anclas; **sin** cuerpos viejos)
- Siguiente tranche de **5** ventanas (texto de peek ya recortado)
- Metadatos: posición en cola (`16/40`), orphans de facets/idents, stems ya reject/useful

Salida (JSON estricto, ejemplo normativo):

```json
{
  "action": "a_judge",
  "verdicts": [
    {
      "target": "src/ui/wake_policy.cpp:should_wake#tail",
      "verdict": "useful",
      "anchor": "src/ui/wake_policy.cpp:90",
      "stem": "wake_policy",
      "role": "primary",
      "why": "early-return ignora flag X"
    },
    {
      "target": "src/ui/wake.cpp:tick",
      "verdict": "reject",
      "why": "solo despacho; no decide política"
    }
  ],
  "done": false
}
```

Cuando cierre:

```json
{
  "action": "a_done",
  "loci": [ /* primary/secondary/suspect */ ],
  "summary": "…"
}
```

A **no** emite `plan`, **no** escribe pack, **no** hace `done next=edit`.

### 4.2 Escaneo estricto (no pedir targets)

| Permitido | Prohibido (modo normal) |
|-----------|-------------------------|
| Juzgar los 5 que manda el runtime | `dame path X` / caza libre / `get_code_of` arbitrario |
| Marcar uncertain | Reordenar la cola a voluntad |
| Early-stop vía `a_done` si loci estables | Acumular cuerpos en sesión / Observations durables |
| Descartar stems enteros tras contraste | Mezclar pack_review en A |

**Escape hatch** (máx. 1–2 / run, runtime valida):

- Sibling de un `useful` (header/caller path del mismo stem)
- `jump_stem` solo si un mini-resumo nombra módulo fuera de ventana **y** el índice lo confirma

Si el escape se usa mucho → arreglar ranking upstream, no ampliar agencia.

### 4.3 Gate de A (distinto de PACK_REVIEW)

| Gate | Pregunta | Señal de éxito |
|------|----------|----------------|
| **A-gate** | ¿Este stem/ancla es el vecindario correcto? | `useful` + contraste; orphans de Instruction bajando |
| **B-gate (`PACK_REVIEW`)** | ¿El pack permite editar/responder? | covered; gaps Instruction↔pack cerrados |

En A, un `reject` es progreso. En B, un reject de fragmento es ruido o miss → micro-A, no “más plan libre”.

### 4.4 Ventanas y funciones largas

Presupuesto de juicio 7B (ver §7): ~40–80 líneas/peek; ~150–250 líneas/vuelta.

Para `body_lines > ~120`:

1. Primer peek = **`#tail`** (no head por defecto).
2. Segundo tramo solo si `uncertain`, reject débil con score alto, o facet hit en otra zona.
3. `#mid` / hit interno excepcional.
4. Reject del símbolo entero solo tras head+tail (y mid si hubo hit).

Los “5” de la vuelta son **ventanas**, no siempre 5 símbolos distintos (p. ej. 3 cortos + `Foo#tail` + `Bar#head`).

### 4.5 Presupuesto de A (evitar 40×2 = 80 peeks)

Worst case ingenuo: 40 símbolos × 2 tramos ≈ 80 peeks / ~16 vueltas. **No es el default.**

| Recurso | Tope habitual |
|---------|----------------|
| Peeks totales | **24–32** |
| Vueltas | **6–8** |
| Peeks/vuelta | **5** |
| Multi-tramo | condicional (§4.4) |

Media objetivo ≈ **1.2–1.4 peeks** por símbolo tocado.

**Early-stop:**

- 2–3 `useful` con contraste (al menos un competidor del shortlist juzgado) → `a_done`.
- 3 vueltas sin useful tras top ~15 → disparar expansión de cola (§5) o clarify.
- No barrer 25–40 por inercia si ya hay loci estables.
- No exigir monogamia de stem: cerrar con **1–2 primary** (+ secondary) es el caso multi-módulo normal.

### 4.6 Estado durable en disco (propuesta)

Bajo `.tuide/ai/l2/`:

```text
a_notes.md      # veredictos + mini-resúmenes (humano/debug)
a_state.json    # cola, cursor, peeks_used, loci_draft, orphans, rejected_stems
```

`session.md` / prompt de A solo ve el slice: notes + tranche actual (no historial de cuerpos).

### 4.7 Relación con L1 `pick_stem` / `context_stem`

- L1 puede seguir proponiendo shortlist / enrich; eso **alimenta la cola de A**, no sustituye A.
- Durante A: **relajar o no aplicar** enrich lock dominante (evita empujar multi-stem al módulo equivocado).
- Un `context_stem` de L1 entra como hint de ranking / prioridad de cola, no como exclusión de otros stems hasta que A emita `a_done`.

---

## 5. Miss de ranking — expansión de cola

Si el locus no está en el top 40, escanearlo perfecto no lo inventa.

### 5.1 Señales de miss

- A vacía / casi vacía al agotar presupuesto.
- Facets o idents de Instruction sin ancla en notes.
- En B: `pack_incomplete` o `PACK_REVIEW` → miss con path/stem **fuera** de `loci[]`.

### 5.2 Capas (runtime, no el modelo)

| Capa | Acción | Cuándo |
|------|--------|--------|
| 0 | Top 40, escaneo 5×5 | siempre |
| 1 | Ampliar 41–80 mismo ranking | A vacía / orphans |
| 2 | Re-rank con needles de Instruction + mini-resúmenes | capa 1 floja |
| 3 | Recall índice local (stem embed + search idents) → +10–20 candidatos | miss de módulo |
| 4 | Micro-A desde miss de B (paths nuevos obligatorios) | ya en pack_review |

Tope: **+2 expansiones** por explore; luego `done next=clarify` con diagnóstico (`A_covered`, `useful`, `orphans`).

Prohibido: “busca tú por el repo”. Permitido: “aquí hay N candidatos que el sistema añadió porque faltaba ident/facet X”.

---

## 6. Fase B — Acumulación rígida (código sabiendo a dónde vas)

### 6.1 Entrada

- `loci[]` de A (anclas obligatorias)
- Brief: mini-resúmenes (compactos)
- Mapa/outlines solo de stems en loci (compactación “hot stems” ya existente)

### 6.2 Comportamiento

Reutilizar y endurecer el camino actual:

1. Runtime convierte loci → `targets[]` must-tier (orden = prioridad primary → secondary).
2. `action=plan` / auto-plan inicial permitido **solo dentro de stems/paths de loci** (+ siblings policy); merge watchlist, normalize bare→símbolo.
3. `apply_plan`: roles, diversity por path, junk→`rejected_targets`, budget pack.
4. **Re-fetch** anclado (`path:Symbol` / `path:line`); no confiar en peeks de A (el mini-resumo puede mentir; el cuerpo en pack no).
5. Profundización dirigida (sustituto de “call hierarchy”): outline del stem locked, defs/refs locales del ancla, headers hermanos, `#mid`/`#tail` si truncado **y** se edita esa ventana.
6. Gates: `pack_incomplete`, `PACK_REVIEW` (covered/partial/miss).
7. Miss fuera de loci → micro-A (§5 capa 4) o pushback; **no** explore libre mezclado (prohibido volver a planear 8 stems nuevos sin señal).
8. Éxito → `done next=edit` (mismo contrato hacia edit/compile).

### 6.3 Lo que B no hace

- No reabre caza de stems sin señal de miss.
- No usa LSP call hierarchy como requisito de cobertura.
- No mete outlines gigantes antes de fragmentos de loci primary.
- No interpreta `Truncated` como pack incompleto si el locus de control/estado ya está cubierto (política actual a preservar).

### 6.4 Anti-patrón que B debe romper (del battery)

```text
plan multi-stem prematuro
  → pack ruidoso
  → PACK_REVIEW partial
  → replan / auto-plan a tests o módulos laterales
  → pushback loops
```

Con A previo: B arranca con must-tier desde `loci[]`; un `partial` pide **más profundidad en loci** o micro-A allowlisted, no un menú MAP/SEARCH que reinserte stems no juzgados.

---

## 7. Capacidad 7B (calibración normativa)

Referencia: Qwen2.5-Coder-7B, `n_ctx` local ~8192 (`L2ContextBudget`: explore ~10k, pack ~9k, `obs_per_turn` ~2.4k; `get_code_of` default 120 líneas).

| Modo | Rango útil |
|------|------------|
| 1 peek | **40–80** líneas (objetivo ~60) |
| 1 vuelta A | **5 × 30–50 ≈ 150–250** líneas |
| Contraste paralelo | ≤ ~3 cuerpos “calientes” |

Recortar peeks al símbolo/ventana; evitar head+tail de 300 líneas en una sola observación. Funciones largas = varios peeks (§4.4).

---

## 8. Embeddings: rol exacto

| Señal | Rol |
|-------|-----|
| Stem embed (`baseline` production) | ¿qué módulos encolar? |
| Body semantic rerank (híbrido) | ¿qué símbolos/ventanas priorizar en la cola? |
| Peek textual | ¿confirma locus? |
| Pack | solo B |

No sustituir el peek por cosine para cerrar A. No meter vectores en el prompt. Passages “ricos” tipo cuerpo en stem-index **no** son el camino (battery: `baseline` gana a `rich_480`).

---

## 9. Multi-stem / bugs dispersos

A cierra con un **set**, no con un único stem:

| Caso | A | B |
|------|---|---|
| Un stem claro | 1 primary | pack anclado |
| Varios archivos, un módulo | 1 stem + N anclas | diversity por path (ya existe) |
| Varios stems (p. ej. UI spinner + cancel controller) | N loci primary/secondary (típicamente 1–2 primary) | unión; **sin** forzar un solo `context_stem` lock en A |

Durante A: relajar o no aplicar enrich lock dominante. El lock de un stem puede reaparecer en B solo como hint de outline, no como exclusión de otros loci.

**No monogamia ciega:** muchos bugs reales cruzan 2 módulos; el plan admite 1–2 primary. Lo que se prohíbe es el plan de 5+ stems “por si acaso” antes de contrastar.

---

## 10. Plan de implementación (fases de ingeniería)

Orden estricto; cada fase tiene criterio de salida comprobable. **No estimar calendario.**

### Fase P0 — Contrato y feature flag

**Trabajo:**

- Flag p. ej. `L2_EXPLORE_PHASE_A` / entrada en `features_promoted.json` (off por defecto).
- Tipos: `AVerdict`, `Locus`, `AState` (cola, cursor, peeks_used, rejected_stems).
- Schema JSON `a_judge` / `a_done` + validación en `l2_action`.
- Docs: este plan + nota corta en `l2-autonomous.md` (“fase A experimental”).

**Salida:** flag compilable; sin flag el explore actual no cambia.

### Fase P1 — Cola desde índice local (sin LSP)

**Trabajo:**

- Builder de cola top-K desde `RepoMap` + stem shortlist + body semantic (si flag embed).
- Política de ventana: corto=body; largo=`#tail` primero.
- Diversify opcional `max_per_stem` para no gastar las 5 ventanas del mismo módulo en vuelta 1.
- Tests unitarios: orden estable, sin llamadas LSP.

**Salida:** dado un fixture de mapa/snapshot, cola determinista 40 ítems con ventanas.

### Fase P2 — Loop A (runtime)

**Trabajo:**

- Sub-fase `explore_a` en session/autonomous loop.
- Inyectar 5 peeks/vuelta; aplicar veredictos a `a_notes` / `a_state.json`.
- Compactar Observations: tirar cuerpos tras juicio; conservar notes.
- Early-stop + tope peeks/vueltas (§4.5).
- Transición `explore_a` → `explore_b` con `loci[]`.
- Banners `L2 ▸ fase=explore_a|explore_b`.

**Salida:** harness/scripted: A barre fixture y emite loci sin escribir `pack.md`.

### Fase P3 — Expansión de cola

**Trabajo:**

- Señales orphans + A vacía.
- Capas 1–3 (§5.2) con contadores y trace.ndjson.
- Clarify diagnóstico si se agota expansión.

**Salida:** caso battery donde gold está en rank 45–60 se recupera vía capa 1/3 sin tools libres.

### Fase P4 — Fase B cableada a loci

**Trabajo:**

- Seed de watchlist/plan desde `loci[]` (must-tier).
- Brief de notes en prompt B (cap estricto).
- Re-fetch obligatorio de anclas.
- `PACK_REVIEW` miss → micro-A (capa 4) con allowlist de paths nuevos.
- Restringir plan libre a stems de loci salvo miss señalizado.
- Preservar `stop_at_explore`, pack_incomplete pushback, diversity.

**Salida:** explore_ok en battery actual ≥ baseline con flag on; casos multi-path no regresan; `17_ai_spinner_stuck` no entra en plan multi-stem prematuro.

### Fase P5 — Desacoplar LSP del camino A/B locate

**Trabajo:**

- Auditar tools/context_pack: paths que requieren LSP en explore.
- Garantizar `get_code_of` / outline / map / siblings / defs-refs vía índice local.
- LSP queda opcional para diagnostics / niceties post-edit.
- Test de integración: LSP disabled → A+B completan en fixture mínimo.

**Salida:** documento de degradación + test “no LSP” verde.

### Fase P6 — Eval / batteries

**Trabajo:**

- Extender `l2_explore_battery`: métricas `A_peeks`, `A_turns`, `loci_hit`, `rank_miss_recovered`, `premature_multi_stem_plans` (debe → 0 en A).
- Casos: single-stem, multi-stem (`expected_stems[]`), long-function-tail, gold fuera de top-40, regresión tipo spinner/cancel.
- Comparar flag off vs on (calidad pack / ready_to_edit, no solo tokens).

**Salida:** informe en `tools/l2_explore_battery` o fixtures; criterio de promoción del flag.

### Fase P7 — Promoción

**Trabajo:**

- Prompt packs / harness prompt actualizados.
- Default flag on en remote o en local según evidencia.
- Retirar o soft-nudge del explore mezclado (tools+plan temprano) cuando Phase A está on.

**Salida:** feature promovida; plan marcado implementado.

---

## 11. Cambios de archivos previstos (orientativo)

| Área | Archivos probables |
|------|--------------------|
| Estado / loop | `src/ai/level2_session.*`, `level2_autonomous_loop.*`, `l2_action*` |
| Cola / map | `repo_map.*`, `coding_embed_rerank.*`, `coding_stem_embed_index.*` |
| Tools | `tool_registry.cpp` (`get_code_of` ventanas head/mid/tail), evitar LSP en A |
| Flags | `tools/l2_battery/features_promoted.json`, feat helpers |
| Docs | `docs/ai/l2-autonomous.md`, `l2-harness-prompt.md`, este plan |
| Eval | `tools/l2_explore_battery/*`, fixtures stem / multi_* |

---

## 12. Riesgos y mitigaciones

| Riesgo | Mitigación |
|--------|------------|
| Mini-resumo mentiroso | Exigir ancla `path:line` / símbolo; B re-fetchea |
| Cerrar A en el primer useful | Exigir contraste (≥1 competidor) |
| 16 vueltas por defecto | Topes 24–32 peeks + early-stop |
| Cola top-40 ciega | Capas de expansión + batteries fuera de top-40 |
| Regresión single-stem fácil | Flag off default; comparar battery antes de promover |
| Reintroducir caza libre vía escape | Cap 1–2; telemetría de uso |
| LSP “por si acaso” en hot path | P5 + test no-LSP |
| Monogamia de stem en bugs 2-módulo | Permitir 1–2 primary; battery multi-stem |
| Coste de pasos A | Compensa si evita packs 9k ruidosos y plan loops (ver battery) |

---

## 13. Fuera de alcance (este plan)

- Cambiar edit/compile/search-replace.
- RAG vectorial global (Fase F master-spec); solo stem/body embed ya cableados.
- Sustituir Tree-sitter por LSP (al revés: **menos** LSP en locate).
- UI nueva del tab AI más allá de banners `L2 ▸ fase=explore_a|explore_b`.
- Re-tunear passages ricos del stem embed battery (seguir `baseline` salvo evidencia nueva).
- Implementar call hierarchy LSP como herramienta de A/B (opcional futuro vía grafo local).

---

## 14. Criterios de aceptación globales

1. Con flag on: ningún `pack.md` se escribe durante `explore_a`.
2. `a_done` produce `loci[]` con anclas resolubles por `get_code_of` vía TS.
3. B solo materializa pack desde loci (+ siblings policy existente).
4. Explore completo sin LSP up en fixture “no clangd”.
5. Budget A: p95 peeks ≤ 32 en battery green; p95 vueltas ≤ 8.
6. Casos multi-stem: recall de `expected_stems` no peor que baseline mezclado (o mejor).
7. Caso long-tail: gold en `#tail` de función >200 líneas se localiza sin dump de 300 líneas en un solo peek.
8. Caso rank-miss: gold en posición 45–70 recuperado por expansión, no por tools libres.
9. Caso regresión explore mezclado: no hay `action=plan` multi-stem (≥3 stems distintos) antes de `a_done`.

---

## 15. Checklist de handoff a implementación

- [ ] Aprobar este plan (contratos JSON + presupuestos + gates A vs B).
- [ ] P0 feature flag + schemas.
- [ ] P1 cola índice local.
- [ ] P2 loop A scripted.
- [ ] P3 expansión cola.
- [ ] P4 B ← loci + micro-A.
- [ ] P5 no-LSP path.
- [ ] P6 batteries + informe.
- [ ] P7 promoción flag.

---

## 16. Mapa chat ↔ documento

| Idea del chat | Dónde queda en el plan |
|---------------|------------------------|
| Separar búsqueda de stem de acumular código | §0, §1.2, fases A/B |
| “Dime stems → itera descartando” | Cola rankeada + `a_judge` reject/useful (§4) |
| Tras stem interesante, pedir código | Transición A→B; re-fetch + profundización (§6) |
| Call hierarchy / símbolos al encontrar stem | Idea sí; medio = índice local, no LSP (§3.1, §6.2) |
| Gates distintos identify vs pack | A-gate vs `PACK_REVIEW` (§4.3) |
| Multi-stem 1–2, no monogamia | §9 |
| Evidencia mínima en identify | Peek efímero; no pack (§2.2, §4) |
| Confusión del battery spinner | §1.1, §6.4, criterio 9 |

---

## 17. Referencias internas

- [PR #10](https://github.com/LorenzoAdr/TIDE/pull/10) — plan original en rama `cursor/l2-explore-phase-ab-plan-0151`
- `docs/ai/l2-autonomous.md` — explore/plan/pack actual
- `docs/ai/l2-harness-prompt.md` — fases explore/edit/compile
- `docs/ai/master-spec.md` — L0/L1/L2, ContextPack D17
- `src/ai/l2_context_budget.*` — caps prompt/pack/obs
- `src/ai/level2_session.cpp` — `apply_plan`, diversity, pack_incomplete
- `src/ai/l2_pack_review.cpp` — gates semánticos de pack (B)
- `src/ai/coding_embed_rerank.*` / `coding_stem_embed_index.*` — embed
- `tests/fixtures/stem_embed_battery/RESULTS.md` — baseline > rich passages
- `.tuide/ai/l2_explore_battery/round_anchor_rescue_v1/console.log` — fallo explore mezclado
