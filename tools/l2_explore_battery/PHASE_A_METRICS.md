# Phase A battery scoring notes (P6/P7)

## Phase A-only full NL battery (recommended)

Stops at `a_done` (no pack B). Scores locate quality of A:

```bash
export L2_FEAT_L2_EXPLORE_PHASE_A=1 L2_FEAT_L2_EXPLORE_EFFECT_SUMMARY=1 TUIDE_ROOT=$PWD
# Overnight / long run (resume skips ids already in results.jsonl):
nohup ./tools/l2_phase_a_battery_run.sh hybrid_a20_v1 \
  > .tuide/ai/l2_explore_battery/round_hybrid_a20_v1/nohup.log 2>&1 &

# Single case smoke:
./tools/l2_phase_a_battery_run.sh smoke_a 17_ai_spinner_stuck only
```

Artifacts: `round_<LABEL>/{CASE}/`, `results.jsonl`, `phase_a_score.json` via
`tools/l2_explore_battery/score_phase_a_battery.py`.

| Metric | Meaning |
|--------|---------|
| `a_done_ok` | Phase A exited cleanly |
| `loci_hit` | fixture `expected_stems` ∩ loci |
| `op_loci_hit` | expected ∪ operational extras (e.g. `busy_strip` on 17/20) |
| `gold_in_map_top15` | operational stem in L1 map top-15 |
| `gold_expanded` | useful note / a1 expand toward gold |
| `a0_coverage_fail` | “faltan veredictos” count |
| `no_premature_plan` | 0 plans while in explore_a |

## Default (full explore, includes pack B)

`L2_EXPLORE_PHASE_A` is **promoted on** in `tools/l2_battery/features_promoted.json`.

```bash
# Forced on (same as default when cwd finds features_promoted.json)
export L2_FEAT_L2_EXPLORE_PHASE_A=1
./tools/l2_explore_battery_run.sh phase_a_on 17_ai_spinner_stuck only

# Classic mixed explore (rollback / A-B compare)
export L2_FEAT_L2_EXPLORE_PHASE_A=0
./tools/l2_explore_battery_run.sh phase_a_off 17_ai_spinner_stuck only
```

Artifacts per case include `a_state.json` / `a_notes.md` when Phase A ran.  
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

Promoted when on-round `explore_success_rate` ≥ off-round and `premature_multi_stem_plans_total == 0`.

Rollback: set `"L2_EXPLORE_PHASE_A": false` in `features_promoted.json` or
`export L2_FEAT_L2_EXPLORE_PHASE_A=0`.
