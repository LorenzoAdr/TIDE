# Piloto de olas: ir más seguro y más rápido

Evidencia: `.tuide/ai/l2_wave/round_spinner6` (caso `17_ai_spinner_stuck`). 8 proposes, 7 olas aplicadas, `done=no`, sin `cierre.md`. El circuito ON/OFF estaba en el cuaderno hacia la ola 4–5; las tres últimas no añadieron control.

El ensayo demuestra que el 27B **sí** elige latch/caller y **sí** pide cuerpos. Se atasca porque el cuaderno crece, la señal causal es sucia, y explorar sigue siendo más barato que cerrar. Esto no es específico del spinner: es el loop.

## Qué hay que cambiar (genérico)

Tres palancas. Ordenadas por efecto / esfuerzo.

### 1. Vista de trabajo (el propose no es un archivo)

Hoy el user prompt de la ola 8 son ~43k chars. Notas peek + Causal = 68 % del cuaderno. Diario + needles = 6 %. Atlas + inspect + candidatas se reinyectan **enteros** cada ola. `n_ctx=8192` no da para eso.

**Hacer**

- Dos vistas: **archivo** (lo que ya se escribe en `wave_N/notebook.md`) y **trabajo** (lo que ve el piloto).
- Trabajo, en este orden, tope ~8–12k chars:
  1. Consulta
  2. Circuito extraído (ON / OFF / callers / huecos) — ver §2
  3. Diario (una línea por ola)
  4. Needles (hits, no dumps)
  5. Último peek (1 cuerpo). El resto: ids ya leídos, no el texto
  6. Abiertos compactos (owns/nucleus/port). Inspect gordo solo en cover o si el piloto pide `peek` de una ficha
  7. Candidatas recortadas a las que no son atlas-only, o a las de la última aguja
- Causal gordo y mermaid: **no** en el propose. Quedan en el archivo. El circuito cita 1–2 stacks.
- Recorte head+tail del cuaderno de trabajo, no solo de un peek.

**No hacer:** subir `n_ctx` para compensar. El modelo se pierde en el medio.

### 2. Hechos extraídos, no dumps de workers

El peek inyecta `aguas_arriba` **mismo stem**. En spinner6, `begin_thinking` aparece llamado desde `download_models`. El camino del chat es `handle_route → run_level1_async`. El follow cruza stems pero, para `clear_busy`, trae search_panel y LSP. En el cuaderno queda además `siguiente: {"action":"causal_pilot_dataflow",…}` — una acción de otro loop.

El piloto no puede fiarse de «quién llama». Entonces pide más peek/follow, o se va a catch/throw.

**Hacer**

- Tras cada peek/follow, el runtime escribe una ficha **Circuito** (hechos, no prosa):

  ```
  ON:  begin_thinking → set_busy_spinner(AiThinking)
  OFF: end_thinking → clear_busy_if(AiThinking)
  callers ON:  run_level1_async, run_level2_followup_async, end_download
  callers OFF: run_level1_async (cola del lambda), …
  huecos: caminos de begin_* sin end_* visto
  ```

- `aguas_*` del peek: o se quitan (el gesto `follow` es el causal) o se etiquetan `mismo stem, incompleto`.
- `follow`: filtrar stacks a stems/símbolos de la consulta y de las zonas keep. Un `clear_busy` del search panel no entra en un caso de spinner de chat.
- No copiar al cuaderno del piloto líneas `siguiente:` / JSON de workers.
- Needle que parece **campo** (`foo_`, hits=0 en NodeId): no es evidencia de «no existe». Mensaje: *no es un nodo; grep `in` en el último peek* — o reroute automático.
- Cuando ON y OFF están anclados, el runtime puede correr `entre` solo (un hecho más). El piloto no tiene que gastarlo.

**Cover:** al abrir 1–2 fichas, inyectar el cuerpo **corto** de los `peek:` de la ficha (begin/set_busy, no el inspect entero). Ahorra la ola de «buscar el apagado por nombre».

### 3. Cerrar de verdad (barreras, no prosa)

En spinner6 el user ya decía «Ya hay encendido y apagado. Preferí cerrar». El modelo hizo peek/needles igual. `ÚLTIMA OLA` no salió: `wave_n` cuenta applies; el fallo de la ola 2 no incrementa `wave_n` pero sí consume un propose. Al proponer la 8, `wave_n=6 < 7`.

El system prompt **planta** catch/throw como ejemplo de `in`. La ola 6 es esa plantilla.

**Hacer**

- Presupuesto = **proposes**, no applies. La última propose legal es `cerrar` (o se sintetiza un cierre del circuito si el JSON no llega).
- Si el circuito tiene ON y OFF: `do` legal ∈ {`cerrar`} más, como mucho, **un** `follow`/`entre` de un hueco nombrado. Needles catch/throw/terminate/abort: barrera.
- Familia excepción: **una** ola de `in` grep. hits=0 cierra la hipótesis. No vale `std::terminate` después de `catch`.
- Ejemplos del system prompt: agujas de **mecanismo** (`set_busy_spinner`, `end_thinking`). Nunca catch/throw/abort.
- Tanda: un `in` inválido no tumba peeks (ya en código; spinner6 no lo llevaba).
- `stem::módulo` = campo, no `in` (ya en código).

## Orden de implementación

1. Vista de trabajo + tope de chars (sin esto, el resto se ahoga).
2. Ficha Circuito + quitar fugas de worker + no plantar catch en ejemplos.
3. Barreras de cierre (propose count, circuito completo, familia excepción).
4. Follow filtrado + auto-`entre` + auto-peek corto en cover.
5. Harness: métricas por corrida — `proposes`, `applies`, `chars_user`, `wave_circuito`, `waves_tras_circuito`, `cerró`.

Criterio de éxito en el mismo caso 17: circuito en ≤4 proposes, `cerrar` en ≤6, `cierre.md` con ON/OFF y el caller del chat (`run_level1_async`), sin olas de catch/throw.

## Fuera de este plan

Workers/`preguntar`, plenario, sustituir atlas-survey. Si la vista de trabajo se lee, el siguiente paso sigue siendo colgar `preguntar` en el mismo loop — pero con un piloto que ya sabe cerrar.
