# Core5 gates — rúbricas agente supervisor

Batería: `tests/fixtures/stem_boost_battery/prompts_nl_human_core5.json`  
Runner: `tools/l2_core5_battery.py` / `tools/run_core5_plan.sh`

## Gate 1B — L1 ProblemFrame

**Automático:** `score_problem_frame.py --check-gate`

| Métrica | Umbral |
|---------|--------|
| schema_ok | 5/5 |
| search_terms_hit_gold | ≥4/5 |
| pass | ≥4/5 |
| mandatory (17, 20) | 2/2 pass |

**Agente — solo mejoras genéricas:**
- Prompt L1 (`level1_agent.cpp` system destilación)
- Fallback `l2_problem_frame.cpp`
- Validación schema / `filter_distilled_ignore`
- **Prohibido:** stems por case id, `OPERATIONAL_EXTRA` en scorers

**Peores casos:** revisar `problem_frame.json` + `search_miss` en score JSON.

## Gate 2 — Grafo anchor_hunt

**Automático:** `score_anchor_graph.py --check-gate`

| Métrica | Umbral |
|---------|--------|
| gold_in_map_top15 | ≥4/5 |
| hop0_gold_hit | ≥4/5 |
| mandatory (17, 20) | 2/2 pass |

**Agente:**
- `GraphQueryProfile::anchor_hunt` wiring
- Bootstrap rerank / symptom_edge
- Registry ingest desde map L1
- **Prohibido:** forzar stems por caso en query

## Gate 3B — F1 caza runtime

**Automático:** `score_f1_anchor.py --check-gate`

| Métrica | Umbral |
|---------|--------|
| pass (f1_ok + anchor hit + no trail) | ≥3/5 |
| mandatory (17, 20) | 2/2 pass |

**Agente:**
- Prompts F1 en `level2_autonomous_loop.cpp`
- `a_f1_coerce_expand_modality`, `a_validate_f1_anchor_done`
- Bootstrap `--problem-frame-json` propagation
- **Prohibido:** relajar scorer solo para un id

## Bucle supervisor

```
RUN → score JSON → leer 2 peores + 1 mejor → ≤2 cambios genéricos → re-run canario (17) → re-run core5
```

Escalar al usuario si un gate falla **2 iteraciones seguidas** o falta LLM.
