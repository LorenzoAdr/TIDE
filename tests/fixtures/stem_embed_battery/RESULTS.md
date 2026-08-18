# Stem-embed battery results (2026-08-15)

Offline IR on TIDE workspace: 35 qrels, 454 stems, nomic-embed-text-v1.5, metric = pure cosine `top_k` (no lexical fuse).

## Ladder

| profile | Hit@1 | Hit@3 | Hit@5 | MRR | chars total | ×baseline |
|---------|------:|------:|------:|----:|------------:|----------:|
| **baseline** | **0.686** | **0.857** | **0.857** | **0.757** | 111858 | 1.00 |
| type_first | 0.600 | 0.800 | 0.857 | 0.710 | 114017 | 1.02 |
| sig_snip | 0.600 | 0.743 | 0.857 | 0.693 | 155501 | 1.39 |
| module_blurb | 0.571 | 0.800 | 0.829 | 0.690 | 114871 | 1.03 |
| hdr_doc | 0.543 | 0.829 | 0.857 | 0.680 | 120855 | 1.08 |
| rich_480 | 0.543 | 0.743 | 0.800 | 0.658 | 158138 | 1.41 |
| rich_720 | 0.543 | 0.743 | 0.800 | 0.658 | 196125 | 1.75 |
| kitchen_sink | 0.514 | 0.714 | 0.829 | 0.636 | 231066 | 2.07 |

## Sweet spot

**Keep `baseline` as production default.** No enrichment met the promotion bar (+5–8% relative Hit@3/MRR without regressing easy Hit@1). All richer profiles diluted identity signal (stem + path + bare names) that nomic uses for short NL queries.

- Cost ceiling ≤1.5× would still allow `rich_480`, but quality drops (~MRR −13% relative).
- `kitchen_sink` is a quality *and* cost regression (chars ~2.1×).

## Notable deltas

Regressions baseline Hit@3 → `rich_480` miss: `perf_threads_es`, `packet_monitor_es`, `visual_highlight_es`, `syntax_highlight_es`, `embedding_backend_es`.

Only clear win for `rich_480`: `wake_redraw_en` (module blurb / policy prose helps conceptual wake queries).

## Next experiments (not promoted)

1. Measure with `fuse_coding_stems` (lexical + cosine), not pure `top_k`.
2. Selective blurb only when stem tokens are generic (`panel`, `util`, …).
3. Type priority **without** the `class:` token prefix (suspected noise).

Artifacts: `.tuide/ai/stem_embed_battery/round_*/` and `compare.json` (local, not committed).
