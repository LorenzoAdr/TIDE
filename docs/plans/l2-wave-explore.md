# Olas de exploración (ensayo de concepto)

Piloto adaptativo aislado. Con atlas de fichas, la **ola 0** es cobertura (think Off): elige 1–2 M* a ampliar. Después cada `propose` es una ola de estrategia (think Medium). No entra en el loop L2 de producción, ni en workers/`cubre`/`como`.

## Qué demuestra

La estrategia no se congela al recibir el prompt. Primero se abren las fichas cuyo owns cubre el objeto; el piloto ve un cuaderno (atlas rico, fichas ampliadas, candidatas, peek) y elige el siguiente `do`. Las fichas de zona **siembran** candidatas (ola 0 del runtime): hipótesis de retrieval, no veredicto. Cada candidata lista `files` (cpp + header par); peek de un `.hpp` lee la API.

## Contrato

```json
{"action":"ola_v1","do":"needles|juicio|peek|follow|entre|tanda|cerrar","campo":"…","needles":["…"],"in":"src/ai/ai_controller.cpp:run_level1_async","keep":["id"],"drop":["id"],"peek":"path.cpp:Symbol","peeks":["M1"],"follows":["M1"],"from":"begin_thinking","to":"end_thinking","why":"…"}
```

Ola 0 (si hay atlas): solo `juicio` keep 1–2 ids. Think: `causal_atlas_cover` = Low. Inyecta fichas ampliadas (`opened_pack` + inspect).

Olas siguientes:

- `needles`: siempre (campo opcional; recorta stem/path). Con `in` = **símbolo** anclado (`path.cpp:fn` o `begin_thinking`), grep **dentro del cuerpo** (catch/throw/…, borde de palabra); hits=0 cuenta. PROHIBIDO `in` de un .cpp suelto y de `stem::módulo` (eso es campo). En una **tanda**, un `in` inválido no cancela los peeks.
- `juicio`: candidata (vale semilla atlas); keep/drop solo ids del cuaderno.
- `peek`: 1–3 loci. Función = `get_code_of` Auto (head+tail si el cuerpo supera ~120 líneas) + firma corta `aguas_arriba` (quién llama, **mismo stem**) y `aguas_abajo` (writes/reads del recorte). Header = API del `.hpp`.
- `follow`: callers (quién llama) y callees (qué llama, con cond). Stacks A-trail, recortes, ramas ON/CXL/OFF, mermaid. Hops peekables. No es la firma del peek ni `entre`.
- `entre`: camino dirigido `from` → `to` en el registry (`registry_path_between`, depth ≤ 8). Los extremos tienen que estar anclados. `sin camino` cuenta. Hops intermedios peekables.
- `tanda`: needles y/o peeks y/o follows y/o entre en la misma ola (evita un propose por gesto).
- `cerrar`: siempre; termina.

Think: fase `causal_wave_pilot` = Medium (512). Ola 0 cover sigue Off.

`needles` usa `registry_query` con `match_surface=NodeId` (substring, no cosine), salvo `in`: grep en el span Tree-sitter del método (firma + inicio + hits con ámbitos AST + cola). `peek` usa `get_code_of` Auto (archivo entero si el peek es un path; función larga = firma + cola). El cuaderno recorta por caracteres en head+tail, no solo la cabeza. `follow` reutiliza el bloque `causal` del worker (`a_trail_causal_flow_markdown`: stacks + recortes + ramas + mermaid). `entre` usa `registry_path_between`.

El atlas en el prompt es `causal_atlas_v1` (kind, ov=, nucleus, port, files), no el resumen de 3 líneas.

Con `--case`, si existe `.tuide/ai/l2_explore_battery/round_atlas20_hyp/cards/<id>/judge_cards.json`, se siembra solo. `--cards FILE` o `--cards-from DIR` fuerza la ruta (falla si no está).

## Cómo correr

Registry ya poblado. L2 remote (27B) en `.tuide/config.json`.

```bash
cmake --build build --target l2_wave_test l2_harness_cli l2_think_test -j$(nproc)
./build/l2_wave_test
./build/l2_think_test

# Caso 17 (latch del spinner; siembra atlas de round_atlas20_hyp si está)
./build/l2_harness_cli wave-explore --case 17_ai_spinner_stuck \
  --out .tuide/ai/l2_wave/round_spinner1

# Misma siembra explícita
./build/l2_harness_cli wave-explore --case 17_ai_spinner_stuck \
  --cards-from .tuide/ai/l2_explore_battery/round_atlas20_hyp/cards \
  --out .tuide/ai/l2_wave/round_spinner1

# Sin ancla (freeze): sin fichas, o con fichas si el mapa no cuadra → needles
./build/l2_harness_cli wave-explore \
  --prompt "a veces la aplicación se me queda congelada" \
  --out .tuide/ai/l2_wave/round_freeze1
```

Artefactos: `wave_N/{ola.json,raw.txt,notebook.md}`, `log.json`, `cierre.md`.

Se espera: spinner con atlas → ola 0 keep M1/M7, luego tanda peek+needles (header `busy_strip.hpp` válido). Freeze → agujas de mecanismo (`join`, `wait`, …) si el atlas no cuadra.

## Fuera de este ensayo

Workers/`preguntar`, `trepar`/`rebuscar`, plenario, sustituir `atlas-survey`. Si el cuaderno se lee bien, el siguiente paso es colgar `preguntar` en el mismo loop.
