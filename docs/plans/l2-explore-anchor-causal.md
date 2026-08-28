# L2 explore: ancla primaria → causal (F1/F2)

**Estado:** F1 implementado (flag off por defecto)  
**Flag:** `L2_EXPLORE_ANCHOR_CAUSAL` (requiere `L2_EXPLORE_PHASE_A` + `L2_EXPLORE_EFFECT_SUMMARY`)

## Contratos

- `problem_frame_v1` → `.tuide/ai/l2/problem_frame.json` ([`src/ai/l2_problem_frame.hpp`](../src/ai/l2_problem_frame.hpp))
- `GraphQueryProfile`: `anchor_hunt` (hops=0), `causal_survey` (hops=2) ([`src/ai/l2_graph_query_profile.hpp`](../src/ai/l2_graph_query_profile.hpp))
- Cierre F1: `f1_done` (1 primary) | `anchor_miss_v1` (fallo explícito)

## F1 anchor hunt

1. Bootstrap construye ProblemFrame (L1 distill / fallback determinista).
2. Cola A rerankeada por `search_terms` + `symptom_edge`.
3. A0: expand **solo peek**; prohibido trail/dataflow.
4. Gate: `a_validate_f1_anchor_done` (relajado vs `a_done` clásico).

## Battery F1

```bash
export L2_FEAT_L2_EXPLORE_ANCHOR_CAUSAL=1
chmod +x tools/l2_f1_anchor_battery_run.sh
./tools/l2_f1_anchor_battery_run.sh smoke_f1 17 only
```

Score: `tools/l2_explore_battery/score_f1_anchor.py`

## Tests

```bash
cmake --build build --target l2_problem_frame_test l2_explore_a_test l2_action_test
./build/l2_problem_frame_test
./build/l2_explore_a_test
./build/l2_action_test
```

## Fuera de alcance (F2)

- `causal_focus_v1`, subfase `f2_causal`, comprehension gate → siguiente iteración.
