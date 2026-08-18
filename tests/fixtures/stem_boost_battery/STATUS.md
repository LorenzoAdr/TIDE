# Stem boost battery — iteration log

## Goal
Measure quality of stem boost (shortlist fuse + map priors) on 10 prompts. Up to 10 iterative tests; apply only clear non-regressive improvements. Stop early when gains are not profitable.

## Metrics (higher better except trap)

| round | hit@1 | hit@3 | MRR | enrich | trap | map MRR | notes |
|-------|------:|------:|----:|-------:|-----:|--------:|-------|
| t01 | 0.60 | 0.80 | 0.70 | 0.60 | 2 | 0.78 | Baseline boost |
| t02 | 0.80 | 1.00 | 0.90 | 0.80 | 1 | 0.83 | NL lexicon + stem-token hits + specificity |
| t03 | 0.90 | 1.00 | 0.95 | 0.90 | 1 | 0.83 | Expanded facet haystack + generic-panel preference |
| **t04** | **1.00** | **1.00** | **1.00** | **1.00** | 1 | **0.88** | Full NL expand into shortlist query haystack |
| t05 | 1.00 | 1.00 | 1.00 | 1.00 | 1 | 0.83 | Map-prior specificity — **reverted** (map MRR down, trap remained) |

## Improvements kept (as of t04)

1. [`search_needles.cpp`](../../../src/ai/search_needles.cpp): rendimiento/hilos→performance/thread; packet+monitor (no blind transport); wake/redraw/async/politica→wake/policy.
2. [`coding_embed_rerank.cpp`](../../../src/ai/coding_embed_rerank.cpp) shortlist: stem-token hit weight, specificity bonus (`ui_wake_policy`), generic-panel preference, NL-expanded query haystack.

## Remaining known gap

`02_wake_policy`: shortlist + `context_stem` correct (`ui_wake_policy`), but map unique-stem order may still list `ui_wake` above it (trap flag). Fixing map order without hurting other wake queries was not profitable in t05.

## Stop decision

Battery **stopped after t05** (5/10). Primary metrics saturated; further map-prior surgery compromised map MRR without clearing the last trap.
