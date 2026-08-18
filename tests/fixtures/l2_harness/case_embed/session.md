# L2 session (harness)

## Tool guide

Whitelist L2 (fixture).

| tool | arg |
|------|-----|
| get_code_of | path:Symbol |

## Instruction

query: dónde se arma el embed / two-stage L1

instruction: Elige del mapa y lee cuerpos con get_code_of.

## Ranked map

query: dónde se arma el embed / two-stage L1

1. src/ai/coding_embed_rerank.cpp:10 — `rerank_map_two_stage`

## Observations

### turn 1 — `file_outline` `src/ai/coding_embed_rerank.cpp`

```
outline fixture
fn rerank_map_two_stage :10
```

### turn 2 — `get_code_of` `src/ai/coding_embed_rerank.cpp:rerank_map_two_stage`

```
stub body src/ai/coding_embed_rerank.cpp:rerank_map_two_stage
```

### turn 3 — `search` `rerank_map_two_stage`

```
hits fixture
```

### turn 4 — done

two-stage in coding_embed_rerank.cpp + level1_agent.cpp
