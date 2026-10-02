# Mechanism pack calibration — iteration log

## Goal

Recover causal story in zone cards via generic mechanism skeleton + cup ranking.
Calibrate knobs globally (no per-case hardcode). Stop at equilibrium.

## Metrics (auto)

| metric | meaning |
|--------|---------|
| story_cover_proxy | gold stem in mechanism ∪ ports |
| skel_gold_touch | gold stem in mechanism slots |
| skel_slot_fill | fraction of trigger/state/effect filled |
| trap_in_mechanism | trap stem appears in mechanism/ports |
| support_gold_density | share of support edges touching gold |
| port_present | ports nonempty when bridges exist |
| budget_waste | redundant/hubbish support edges |

## Cursor rubric

See [CURSOR_RUBRIC.md](CURSOR_RUBRIC.md). Scores: `cursor_scores_t2.json`.

## Stop rules

1. story_cover_proxy and skel_gold_touch do not improve ≥2pp for 2 rounds, or
2. mean story_clarity ≥4 and mean noise ≤2 with trap_in_mechanism not worse than baseline, or
3. story gains hurt traps/density → revert and stop.

Max rounds: 8.

## Rounds

| round | knobs focus | story_cover | skel_gold | slot_fill | traps | notes |
|-------|-------------|------------:|----------:|----------:|------:|-------|
| t0 | pack on, w_cos=40 w_ppr=30 | 12/20 (0.60) | 11/20 (0.55) | 0.356 | 5 | Baseline + slot scoring prefs (ctrl/handoff/read) |
| t1 | structure-dominant (low semantic) | 12/20 (0.60) | 11/20 (0.55) | 0.356 | 6 | No story gain; traps slightly worse |
| **t2** | **w_cos=55 w_ppr=45 floor=45** | **13/20 (0.65)** | **11/20 (0.55)** | **0.356** | **5** | **Best cover; traps held** |
| t3 | w_redundancy↑ port_cup=1 w_hub↑ | 12/20 (0.60) | 11/20 (0.55) | 0.356 | 5 | Cover/density down — **reverted** |

Cursor t2 (8-case sample): mean story_clarity 3.13, noise 2.75, tune_count 2 (cancel + compile traps). Worst cases are mostly **upstream nucleus miss**, not cup ranking.

## Improvements kept

1. `RegistryCausalJudgeOpts` mechanism pack fields + `registry_causal_judge_opts_apply_json`.
2. Skeleton slots `trigger`/`state`/`effect` + `ports` + `support_edges`; `edges` = concat.
3. Slot scoring prefs: ctrl/handoff over bare call; effect prefers same-member read; hub call penalty.
4. Markdown: `mechanism:` / `ports:` / `## zone bridges`.
5. CLI `--judge-knobs FILE`.
6. **Frozen knobs = t2** (`w_cos=55`, `w_ppr=45`, `semantic_hard_floor=45`) as C++ defaults.

## Remaining known gaps

- `skel_slot_fill` ~0.36: many M* are `no_state_nucleus` → empty skeleton by design.
- Cases like `20_cancel_ai_generation`: gold stems not in packed nuclei — needs retrieval/merge upstream, not more edge ranking.
- Trap pollution in a minority of packs when gold and trap share busy UI stems.

## Stop decision

**Stopped after t3 (4/8).** t2 is equilibrium: +5pp story_cover vs t0 with traps unchanged; t3 regressed cover and support_gold_density (rule 3). Further knob axes would chase upstream recall. Frozen defaults = t2.
