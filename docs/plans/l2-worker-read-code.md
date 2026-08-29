# Worker que lee código

Las aristas de la ficha **no** son evidencia de comprensión. El modelo pide `need_code` / `dataflow` / `causal`; el runtime solo ejecuta lo que él emite. Un `follow` no desbloquea el informe.

No es la batería atlas 20 ni el plenario. No auto-ejecutar tools.

## Contrato

Informe `ok` solo con cuerpo (`----- need_code -----`) **y** flujo (`----- dataflow -----` o `----- causal -----`) en las notas.

Prompt: hops de ficha no bastan. Prohibido *«ya hay tool, no pidas otra»*.

Tools nuevas: `causal_pilot_dataflow` (variable), `causal_pilot_causal` (diagrama desde fuente). Reusan Explore A (rg, no SSA).

## Puertas (antes de la batería de 13)

Fixture `tests/fixtures/pilot_worker_read/` (latch vs chrome). Gold = el propio fuente.

- A: test sin LLM (peticiones simuladas + canned).
- B: LLM sobre el mismo árbol; si solo sigue aristas, falla.

## Batería (después)

`python3 tools/l2_worker_probe.py --pack items --cycle N --label read_code`

`gold_keep` no puede bajar. Un ciclo = un parche. Prohibido hardcodear stems.
