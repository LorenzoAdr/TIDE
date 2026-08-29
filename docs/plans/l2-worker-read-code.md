# Worker que lee código

Las aristas de la ficha **no** son evidencia de comprensión y **no** se premian: un `follow` no desbloquea el informe. El modelo pide `need_code` / `dataflow` / `causal`; el runtime solo ejecuta el JSON que él emite (nada de auto-outline ni auto-dataflow). `cubre`, `como` y `gap` leen código antes del veredicto.

No es la batería atlas 20 ni el plenario. No auto-ejecutar tools.

## Contrato

Informe `ok` solo con cuerpo (`----- need_code -----`) **y** flujo (`----- dataflow -----` o `----- causal -----`) en las notas. Si informa antes, parse rechaza y un nudge pide tool; el modelo elige cuál.

Prompt: hops de ficha no bastan. Prohibido *«ya hay tool, no pidas otra»*.

Tools nuevas: `causal_pilot_dataflow` (variable), `causal_pilot_causal` (diagrama desde fuente). Reusan Explore A (rg, no SSA). El search del worker usa el `root` del encargo (`root/src`), no un `src/` hardcodeado del repo TIDE.

## Puertas (antes de la batería de 13)

Fixture mini-workspace `tests/fixtures/pilot_worker_read/src/` (`latch.cpp`: `start_busy` / `tick` / `halt_busy` sobre `busy`; `chrome.cpp`: hover que no toca `busy`). Layout `src/` para que el filtro srcish de Explore A aplique sin tocar SSA. Gold = el propio fuente.

- A: test sin LLM (peticiones simuladas + canned).
- B: LLM con `root` = ese árbol; si solo sigue aristas, falla.

## Batería (después)

`python3 tools/l2_worker_probe.py --pack items --cycle N --label read_code`

`gold_keep` no puede bajar. Un ciclo = un parche. Prohibido hardcodear stems.
