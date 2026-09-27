# Plan: reducir vueltas y mejorar precisión en piloto/explorador/verificador/refutador

> **Estado:** propuesta (sin implementar). Origen: análisis crítico de `admin_v1`
> (`src/ai/l2_admin.cpp`) a partir del historial reciente de tuning por anécdota
> (`c0c0dc7`, `043904f`, `4f461b8`, `30df1ce`).
> **Objetivo:** bajar el número de round-trips LLM por consulta sin relajar el
> criterio de aceptación (el verificador/refutador debe seguir siendo tan
> estricto o más).

## 0. Resumen del sistema (contexto)

Un solo agente de producto (`admin_v1`) con tres roles LLM separados por
system prompt (no tres modelos):

| Rol | Qué hace | Presupuesto actual |
|---|---|---|
| **Piloto** (`run_admin_loop`) | 1 JSON/turno: `spawn/cerrar/editar/confirmar_editar/seguir_explorando/ask_user` | `proposes≤12`, `spawns≤8` |
| **Explorador** (`admin_run_explore_lite`) | Hijo con mini-loop `grep→read→cerrar`, veredicto tipado `encontrado\|no_encontrado\|parcial` | `≤4 explores/consulta`, `≤6 pasos`, `≤4 grep`/`≤3 read` totales |
| **Verificador** (`admin_run_verify`) | Gate adversarial pre-`editar`/`cerrar`: `entre`/`read`/`cerrar` | `≤2 pases/consulta`, `N=6` pasos/pase (+1 si hay miss) |
| **Refutador** | Si el verificador dice `sostiene`, 2ª llamada LLM completa que intenta tumbarlo | 1 llamada extra por cada `sostiene` |

Coste peor caso por consulta: 12 (piloto) + hasta 4×6 (exploradores) + hasta
2×7 (verificador) + 2 (refutador) ≈ **50+ round-trips LLM**.

## 1. Diagnóstico crítico

1. **No hay JSON constreñido en el hot path actual.** Existe infraestructura
   ya construida y probada — `src/ai/l2_grammar.cpp` +
   `L2BrainRequest::grammar_file` (GBNF vía `llama-cli --grammar-file`) — que
   usa el loop legado (`level2_autonomous_loop.cpp:1660`) para fases
   `edit`/`compile`. **`l2_admin.cpp` nunca la usa** para piloto, explorador,
   verificador ni refutador. Cada "JSON inválido, reemite" es una vuelta
   evitable con lo que ya tienen.
2. **El refutador es un impuesto fijo sobre el camino feliz.** Cuando el
   verificador dice `sostiene` (la respuesta es correcta), se dispara una
   llamada LLM extra con su propio system prompt y su `facts_blob`
   reenviado entero. El caso en que todo salió bien es el más caro.
3. **Los exploradores no se ven entre sí dentro de la misma consulta.** Si el
   piloto parte el pedido en varios "polos" y lanza varios `explore`, cada
   hijo arranca sin saber qué greps/paths ya cazó un hermano — riesgo de
   grep redundante.
4. **El presupuesto del verificador mezcla pasos productivos con pasos
   perdidos.** Un `entre`/`read` válido y un "JSON inválido, reemite"
   consumen el mismo cupo de `N`. Subir `N` (4→6, `043904f`) tapa el
   síntoma pero diluye el presupuesto real entre trabajo y ruido de formato.
5. **No hay pre-filtro determinista antes de invocar al LLM verificador.** El
   código ya sabe en C++ si hay `miss_jobs` (`no_encontrado`/`parcial`)
   antes de llamar al modelo, pero igual gasta la llamada completa y solo
   *después* hace la contra-pregunta.
6. **Duplicación C++ ↔ Python.** `tools/l2_wave/verify_lite_local.py` /
   `explore_lite_local.py` replican a mano la lógica de `l2_admin.cpp` para
   las baterías offline. Varios commits recientes son literalmente "portear
   el fix de un lado al otro"; riesgo de divergencia silenciosa.

El patrón de commits recientes (subir `N`, "never absuelve as sostiene",
"ignore refute-pass JSON failures") es **tuning reactivo por anécdota**: se
ajusta un contador porque un caso concreto se atascó, sin medir dónde se
queman realmente los pasos.

## 2. Propuestas (orden de ataque recomendado)

### P1 — Grammar-constrained JSON en las 4 llamadas de `l2_admin.cpp`
*Retorno: alto. Riesgo: bajo.*

Extender `l2_grammar::resolve_for_phase` (o GBNF nuevos por fase: `admin`,
`admin_explore`, `admin_verify`, `admin_verify_refute`) y setear
`req.grammar_file` en esas llamadas. Elimina de raíz las vueltas de "JSON
inválido, reemite" sin tocar la lógica de negocio (`why`/`falta`/etc. siguen
siendo obligatorios, solo que el modelo no puede emitir basura sintáctica).
Mayor apalancamiento: la infraestructura ya existe y ya está probada en el
loop legado.

### P2 — Pre-filtro determinista antes de invocar al verificador LLM
*Retorno: alto. Riesgo: bajo.*

Si `miss_jobs` no está vacío, bloquear en C++ con `dudoso` citando el hueco
**antes** de gastar la llamada al modelo, en vez de dejar que el LLM diga
`sostiene` para re-preguntarle después. Ahorra un pase entero de verify
(hasta 7 pasos) cuando la evidencia ya tiene huecos tipados conocidos.

### P5 — Instrumentar antes de seguir tocando constantes a mano
*Retorno: alto para el proceso. Riesgo: bajo.*

(Numerado P5 para mantener la relación con la discusión original — hacerlo
en paralelo/antes de P3/P4/P6.)

Añadir a `trace.ndjson` un resumen por consulta cerrada: `proposes`,
`spawns`, `explores`, `verify_steps` (productivos vs. descartados por JSON
inválido), `verify_passes`, veredicto final. Sin esto, el próximo ajuste de
`N` vuelve a ser "vi un caso colgado, subo el número". Con esto se puede ver
si `N=6` se gasta en pasos productivos o en reintentos de formato (que P1
eliminaría, permitiendo *bajar* `N` en vez de subirlo).

### P3 — Fusionar el refutador dentro del cierre del verificador
*Retorno: medio-alto. Riesgo: bajo-medio.*

En el JSON de cierre del propio verificador, exigir un campo obligatorio de
autocrítica (`"ataque_vecino":{"intentado":"…","tumbado":bool}`) en vez de
una segunda llamada con contexto reconstruido desde cero. Reservar el
refutador como llamada separada solo para el camino `editar` (donde el
coste de un falso positivo es mayor: se toca código). Corta una llamada
completa del camino feliz de `cerrar` de solo lectura.

### P4 — Compartir contexto entre exploradores de la misma consulta
*Retorno: medio. Riesgo: bajo.*

Pasar al brief de cada nuevo `explore` un resumen de 2-3 líneas de lo que ya
cazaron explores anteriores de esa consulta (paths/símbolos del notebook),
para que no re-grep terreno ya cubierto.

### P6 — Presupuesto adaptativo en el verificador
*Retorno: medio. Riesgo: medio. Depende de P1 + P5.*

Solo tiene sentido después de medir con P5: si los reintentos por JSON
inválido desaparecen (P1), probablemente no haga falta `N=6` — volver a
`N=4` o menos, midiendo en vez de adivinando.

### P7 — Reducir el doble mantenimiento C++/Python
*Retorno: proceso. Riesgo: bajo.*

Como mínimo, un test de CI que corra las mismas *cases* de batería contra el
harness Python y (si es viable) contra un modo scripted del C++, para que
una divergencia falle explícitamente en vez de descubrirse por un commit de
"matches C++ admin verify" a posteriori.

## 3. Orden de ataque

**P1 → P2 → P5 → P3 → P4 → P6 → P7**

P1 y P2 son puramente mecánicas (no cambian el criterio de qué es
`sostiene`/`refuta`): no deberían mover la precisión, solo quitan vueltas
desperdiciadas. P3/P4 sí tocan el diseño del gate — conviene medirlas con
P5 antes/después para confirmar que no se pierde rigor.

## 4. Referencias de código

- Piloto/loop: `src/ai/l2_admin.cpp:4181` (`run_admin_loop`)
- Explorador: `src/ai/l2_admin.cpp:1228` (`admin_run_explore_lite`)
- Verificador + refutador: `src/ai/l2_admin.cpp:3512` (`admin_run_verify`)
- Constantes de presupuesto: `src/ai/l2_admin.hpp` (`kAdminMax*`, `kAdminVerify*`)
- Grammar GBNF (infra existente, no usada por admin_v1 hoy):
  `src/ai/l2_grammar.cpp`, `src/ai/l2_brain.hpp` (`grammar_file`),
  uso actual en `src/ai/level2_autonomous_loop.cpp:1660`
- Harness Python espejo: `tools/l2_wave/verify_lite_local.py`,
  `tools/l2_wave/explore_lite_local.py`, `tools/l2_wave/admin_battery.py`
- Doc de producto relacionada: [`l2-admin.md`](../ai/l2-admin.md),
  [`l2-admin-battery.md`](../ai/l2-admin-battery.md)
