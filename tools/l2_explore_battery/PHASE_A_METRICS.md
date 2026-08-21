# Phase A battery scoring notes (P6)

## Enable

```bash
export L2_FEAT_L2_EXPLORE_PHASE_A=1
./tools/l2_explore_battery_run.sh phase_a_v1 17_ai_spinner_stuck only
```

Artifacts per case now include `a_state.json` / `a_notes.md`.  
`score_explore.py` adds `phase_a` metrics when those are present:

| Metric | Target |
|--------|--------|
| `A_peeks` | ≤ 32 |
| `A_turns` | ≤ 8 |
| `premature_multi_stem_plans` | **0** (no `plan` while `explore_a`) |
| `loci_hit` | expected_stems ∩ loci/pack |
| `rank_miss_recovered` | expansions>0 when gold was outside top-K |

## Compare flag off vs on

Run the same `START_AT` twice with different `LABEL` (`phase_a_off` / `phase_a_on`) and diff `explore_score.json` (`explore_success_rate`, `premature_plans`, `facet_recall`).

## Promotion gate (P7)

Promote `L2_EXPLORE_PHASE_A` only if on-round `explore_success_rate` ≥ off-round and `premature_multi_stem_plans_total == 0`.
