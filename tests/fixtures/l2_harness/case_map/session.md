# L2 session (harness)

## Tool guide

Whitelist L2 (fixture).

| tool | arg |
|------|-----|
| get_code_of | path:Symbol |

## Instruction

query: cómo se escribe map_last

instruction: Elige del mapa y lee cuerpos con get_code_of.

## Ranked map

query: cómo se escribe map_last

1. src/ai/get_code_of.cpp:10 — `dump_ranked_map_md`

## Observations

### turn 1 — `file_outline` `src/ai/get_code_of.cpp`

```
outline fixture
fn dump_ranked_map_md :10
```

### turn 2 — `get_code_of` `src/ai/get_code_of.cpp:dump_ranked_map_md`

```
stub body src/ai/get_code_of.cpp:dump_ranked_map_md
```

### turn 3 — `search` `dump_ranked_map_md`

```
hits fixture
```

### turn 4 — done

dump_ranked_map_md writes .tuide/ai/map_last.md
