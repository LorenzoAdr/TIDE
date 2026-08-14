# Stem-embed battery

Offline IR eval for `CodingStemEmbedIndex` passage profiles.

## Dataset

[`qrels.json`](qrels.json): query → `expected_stems[]` (+ optional `hard_negatives`).

## Run

```bash
# Build
cmake --build build --target stem_embed_battery -j

# One profile (embeds via EmbeddingBackend; uses ~/.cache/tuide models)
./tools/stem_embed_battery_run.sh baseline

# Full ladder
./tools/stem_embed_battery_run.sh all

# Passages only (no embed server) — cost stats
./build/stem_embed_battery --profile rich_480 --passages-only
```

Artifacts: `.tuide/ai/stem_embed_battery/round_<profile>/` (`results.jsonl`, `summary.json`).

## Metrics

- Hit@1 / Hit@3 / Hit@5
- MRR (first expected stem in top-K)
- Mean / p95 passage chars, total chars, stem count, embed wall time

## Profiles

`baseline` → `type_first` → `sig_snip` → `hdr_doc` → `module_blurb` → `rich_480` → `rich_720` → `kitchen_sink`

## Latest result

See [`RESULTS.md`](RESULTS.md): **baseline remains the production default** (richer passages hurt pure cosine recall on this golden set).

