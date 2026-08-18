# L2 session (harness)

## Tool guide

Whitelist L2 (fixture).

| tool | arg |
|------|-----|
| get_code_of | path:Symbol |

## Instruction

query: cómo despierta la UI / wake bridge

instruction: Elige del mapa y lee cuerpos con get_code_of.

## Ranked map

query: cómo despierta la UI / wake bridge

1. src/ui/ui_wake.cpp:10 — `wake_ui`

## Observations

### turn 1 — `file_outline` `src/ui/ui_wake.cpp`

```
outline fixture
fn wake_ui :10
```

### turn 2 — `get_code_of` `src/ui/ui_wake.cpp:wake_ui`

```
stub body src/ui/ui_wake.cpp:wake_ui
```

### turn 3 — `search` `wake_ui`

```
hits fixture
```

### turn 4 — done

wake via ui_wake.cpp:10 and bridge
