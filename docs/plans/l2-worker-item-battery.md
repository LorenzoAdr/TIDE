# Batería por ítem (medidas genéricas)

Cada ciclo puntúa **8 ítems** sobre las mismas 13 tiradas del pack. No es la batería atlas 20 ni el plenario.

## Ítems

| id | Qué mide | Casos |
|---|---|---|
| `follow_ficha` | Tool con target de ficha (cero `inventada`) | como cancel/latch/overlay, gaps |
| `empty_follow_rep` | Follow sin hops de un representante ≠ `no_cubre` | latch, overlay |
| `brief_as_why` | Informe `ok` aunque why corto, si el brief cita | welcome DAP, search_replace |
| `no_silent_flip` | `raw_verdict == verdict` | toolpacks, layout |
| `no_repeat_follow` | No repetir el mismo símbolo en tools | como y gap |
| `trap_on_raw` | Trampa: el **raw** es `no_cubre` | chrome, search, DAP, quit, layout |
| `gold_keep` | Hits cubre no regresan | spinner, cancel, toolpacks |
| `walk_gold` | gold_writers en el walk (o `duda:`); trampa layout = raw no_cubre | latch, cancel, layout |

## Cómo correr

Baseline (reusa artefactos del ciclo 3 de robustez, sin LLM):

```bash
python3 tools/l2_worker_probe.py --pack items --cycle 0 \
  --from .tuide/ai/l2_explore_battery/worker_tune/robustness/cycle_3
```

Tras implementar **un** ítem de producto (p.ej. follow vallado a ficha):

```bash
python3 tools/l2_worker_probe.py --pack items --cycle 1 --label follow_ficha
python3 tools/l2_worker_probe.py --pack items --compare 0,1
```

`--compare` imprime `antes → después` por ítem. Sube el numerador = la medida ayudó. Si `gold_keep` baja, hay regresión.

Salida: `.tuide/ai/l2_explore_battery/worker_tune/items/cycle_N/` (`score.json` con `item_scores`, `trace.md`).

## Orden de implementación sugerido

1. `follow_ficha` (parse: follow como need_code, solo notebook)
2. `empty_follow_rep` (sin hops + target en reps → chain u otra tool)
3. `brief_as_why` (why corto, brief bueno → ok)
4. `no_silent_flip` (quitar flip; dejar nudge)
5. `no_repeat_follow` (rechazar el mismo target otra vez)
6. `trap_on_raw` sale solo: es rúbrica, no parche de worker

Un ciclo = un parche. Prohibido hardcodear stems.
