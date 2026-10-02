# Afinado de workers piloto (cubre / como / gap)

**Estado:** probe aislado; bucle Cursor ≤5 ciclos por modo.  
**No es:** batería 20, plenario, `fn_card`, ni reabrir el plan del piloto.

## Objetivo

El worker es un **sensor**. Cada modo pide tools (`need_code` | `outline` | `follow`) con targets de la ficha, acumula notas, e informa sin plantilla.

Tras cubre honesto (la trampa es `no_cubre`) el siguiente producto es el piloto que reacciona a `no_cubre`, no este probe.

## Cómo correr un ciclo

```bash
cmake --build build --target l2_harness_cli l2_effect_registry_test -j$(nproc)
./build/l2_effect_registry_test
python3 tools/l2_worker_probe.py --kind cubre --cycle 1
```

Fichas: `.tuide/ai/l2_explore_battery/round_atlas20_pilot_workers2` (`inspect.json` por caso).  
Casos: `tools/l2_battery/worker_probe/cases.json`.  
Salida: `.tuide/ai/l2_explore_battery/worker_tune/<kind>/cycle_N/` (`score.json`, `trace.md`).  
Estado: `.tuide/ai/l2_explore_battery/worker_tune/state.json`.

## Protocolo Cursor (un ciclo)

1. Leer `state.json` y el `score.json` anterior.
2. `python3 tools/l2_worker_probe.py --kind <mode> --cycle N`.
3. Mirar `w1_raw_*.txt` y `w1_tool_*.md`: ¿target de ficha? ¿inventó? ¿el why usa las notas?
4. Rúbrica OK → siguiente modo, `cycle=1`. Si era `gap` → fin.
5. Si no: **un** parche (prompt de ese kind, o valla genérica, o texto de símbolos permitidos). Prohibido hardcodear stems/casos.
6. Rebuild + `l2_effect_registry_test`, `cycle++`.

Tope 5. Early-stop si 3/3 (o 2/3 solo porque la ficha flaca no estaba vacía).

**Cubre:** si la trampa sigue en `cubre`, no congelar ni pasar a como.

**Como/gap:** al ciclo 5 se puede congelar y seguir.

## Parches prohibidos

Piloto, survey, tally, más de un kind por ciclo, `fn_card`, `set_busy_spinner` u otros símbolos de un caso en el contrato.
