# Worker que lee código

Las aristas de la ficha **no** son evidencia. El modelo pide `need_code` / `dataflow` / `causal`; el runtime solo ejecuta el JSON pedido. El encargo es **enumerar los pasos de la consulta en este stem, sin dudas**. El veredicto sale de esa enumeración. El runtime no firma comprensión: solo impide mentir (un paso del walk que no sea `duda:` debe aparecer en un chunk `----- need_code -----`). Fusible: 20 turnos, el último obliga el walk (`no_cubre`/`missing` y `duda:Symbol` si no llega).

`cubre`, `como` y `gap` leen código. No atlas 20 ni plenario. No auto-ejecutar tools. No rechazar un `chain` porque el dataflow nombró un writer no leído: eso lo puntúa la batería.

## Contrato

Campo `walk`: `sym: acto -> sym: acto`. Duda: `duda:Symbol`. `chain` es alias si `walk` viene vacío.

Parse: follow-only no es lectura; símbolos inventados no; un paso comprendido sin cuerpo es mentira. **No** hay llave `need_code`+`dataflow` para informar. El prompt no invita el informe al segundo tool.

El dataflow nombra la función envolvente (`fn=path.cpp:halt_busy`) y la mete en el notebook; eso no autoriza listarla como paso hasta abrirla.

## Puertas

Fixture `tests/fixtures/pilot_worker_read/src/`. Gold = el fuente (`start_busy` escribe, `halt_busy` limpia).

- A: `l2_effect_registry_test` → `test_pilot_worker_read_gate_a` (notebook incluye `halt_busy`; walk de `halt_busy` no leído = rechazo; walk de un paso leído = ok; `duda:halt_busy` = ok).
- B: iterar `python3 tools/l2_worker_probe.py --pack synth --cycle N --label walk`. El walk debe citar escribe **y** limpia en texto reclamado (no `duda:`, no hop `start_busy -> tick`).

## Batería (cuando B esté verde)

`python3 tools/l2_worker_probe.py --pack items --cycle N --label walk`

Ítem `walk_gold`: `gold_writers` en el walk (o `duda:Symbol`); `empty_cubre_layout` sigue siendo trampa `no_cubre` en raw. `gold_keep` es regresión.

Tres verdes antes de hablar de listón: synth walk completo; un como TIDE con segundo writer o `duda:` de ese símbolo; layout trap honesta.

## Ciclos

- Synth cycle 1 `accumulate`: chain `start_busy -> tick -> halt_busy`. Notebook incluye `halt_busy` vía dataflow.
- Items cycle 3 `accumulate`: 10/13. `gold_keep` 3/3. Suben `brief_as_why` 2/2, `no_silent_flip` 2/2, `no_repeat_follow` 5/5. Sigue fallando `empty_cubre_layout` (trampa cubre) y dos gaps (`why` sin símbolo).
- Walk: prompt de enumeración sin semáforo de cierre body+flow; parse anti-mentira (identificador acotado: `ticks` no es `tick`). Último turno no autoriza un paso no leído.
- Sin cuerpo (o tras un walk que miente): el user prompt **PROHIBIDO** `causal_pilot_worker_v1` y el nudge trae el JSON de `need_code` del símbolo. No es “ya comprendiste”; es “aún no has leído / acabas de mentir, el siguiente JSON es una tool”.
- Synth cycle 8 `walk`: `start_busy: escribe el campo -> halt_busy: limpia el campo`. need_code del clear tras dataflow. El probe no marca `inventada` un `need_code` de un `fn=` del dataflow.
- Items cycle 5 `walk`: 5/13. `empty_cubre_layout` = `no_cubre` fundado. `walk_gold` 1/3 (layout sí; latch copia el hop `ensure_spinner_thread`; cancel `duda:cancel_level1` sin `cancel_current`). `gold_keep` 2/3 (toolpacks: kind inválido).
