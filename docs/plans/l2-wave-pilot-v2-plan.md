# Piloto de control v2: contrato estructurado, medición y escalera de modelos

Estado: en curso (2026-09-21). S0 adoptado (minimal default); B aterrizado.
Sustituye a la iteración por palancas de prompt (sense12–16) como método principal.
Relacionado: [`l2-wave-explore.md`](l2-wave-explore.md), [`l2-wave-pilot-hardening.md`](l2-wave-pilot-hardening.md).

## 1. Punto de partida (medido en el histórico)

Fuente: `.tuide/ai/l2_wave/sense12..sense16` (~113 rondas × 5 cazas; **no versionado**, ver A1).
Las clasificaciones de texto son heurísticas por palabras clave.

| Hallazgo | Dato |
|---|---|
| El contexto del piloto no es el problema | media 14–16 KB por turno, máx. 30 KB; el remoto tiene `n_ctx=32768` |
| Turnos del piloto rechazados por el runtime | 29 % (sense12) → 20–24 % (sense13–16) |
| «Cerrados» del hijo sin veredicto (solo `leído: nombres`) | 42 % de 639 (sense13–16) |
| «Cerrados» que niegan / que afirman | 42 % / 15 % |
| Tope de jobs frente a plan | `kWaveControlMaxJobs=3` frente a `kWaveControlPlanFasesMax=4`; el puente casi nunca corre |
| Deadlock por gate léxico | sense16 r06 caso 11: 6 de 10 turnos rechazados; el `why` final contradice los jobs 2 y 3 |
| Prompt del piloto | ~8 KB, ~40 reglas, varias parches de una ronda; few-shots en el dominio de los casos 17/20 |
| Tendencia del scorer (aciertos/5 por ronda) | sense12 r01–10: 2,90 → r11–50 (40 rondas de parches de prompt y gates): 1,60 / 2,44 / 2,30 / 2,30 → r52–60 (tras «atlas del hijo de SU consulta»): 3,11 → sense13–15: 3,35 / 3,77 / 3,90. Casos «existe» en r41–50: 0,10 de 2. Scorer laxo y n=1: leer como tendencia, no como prueba |
| Efecto por tipo de palanca (media de 3 rondas tras el cambio menos 3 previas; clasificación por palabras clave) | regla de prompt +0,04 (n=33); gate/regla de cierre −0,22 (n=15); estructural +0,22 (n=26); revert +0,11 (n=25). sd ≈ 1,0 por palanca → error estándar ≈ 0,2: **ninguna diferencia es significativa**. Los mayores «saltos» incluyen un revert (+2,5), es decir, ruido |
| Coste de una ronda | mediana 65 min (5 cazas), unas 136 h de reloj en total |
| Ruido | misma configuración, resultados distintos (sense16 r01 frente a r03, `DumpNoCierra` activo en ambas); en sense12 r52–r60 (cambios pequeños) el caso 11 pasa Y Y Y n Y n Y n n |

Diagnóstico: la arquitectura (piloto que no lee + hijos que leen) **aísla bien el contagio**. Los fallos
vienen de (1) un canal hijo→piloto sin veredicto, (2) un cierre validado con regex sobre texto libre,
(3) topes que impiden ejecutar el propio plan y (4) una medición con n=1 y juez subjetivo.

## 2. Principios

1. Más libertad en **qué investigar** (plan, verbos, orden); menos en **cómo se prueba el cierre** (esquema, no texto).
2. El piloto sigue sin leer código. Las comprobaciones las hacen jobs estrechos.
3. Un cambio de harness se acepta por **métricas de proceso o por mejora medida en el modelo objetivo con repeticiones**, no por una ronda.
4. Nada de reglas de prompt a medida de un caso (ya prohibido). Los few-shots salen del dominio de las baterías.
5. Cada fase es reversible detrás de un flag y tiene criterio de aceptación numérico.
6. **Presupuesto de complejidad.** Ninguna regla, flag o tipo de job se queda si no gana a la versión sin él
   (N repeticiones, A4). Por cada regla que entra, sale otra. Hoy el piloto arrastra ~40 reglas y 51 flags
   `kWaveControl*` sin una sola ablación; la carga de la prueba pasa a ser de quien añade, no de quien quita.
7. Cuando una mejora de prompt y una estructural compitan, se prueba primero la estructural: en el histórico,
   40 rondas de parches de prompt no movieron el scorer y el único salto claro fue un cambio de estructura del hijo.

## 3. Fases

Orden recomendado: A → **S0** → B → C → D → G. E y F son **condicionales** (solo si, tras B–C, sigue fallando la métrica que atacan).
H corre en paralelo desde el final de A.

### A. Medición y corpus (P0, sin modelo)

**A1. Archivar el corpus.** `.tuide/` está en `.gitignore`. Exportar un subconjunto curado a
`tests/fixtures/l2_wave/corpus/<sense>_<ronda>_<caso>/` con `control.json`, `jobs.md`, `plan.json` y el
`user.md` del último turno del piloto. Incluir un `MANIFEST.json` (commit, flags activos, modelo).
Aceptación: ≥ 200 trazas archivadas y cargables desde un test.

**A2. Métricas de proceso.** Nuevo `tools/l2_wave/process_metrics.py` sobre cualquier carpeta de rondas:
- fracción de turnos rechazados y motivo;
- fracción de Cerrados sin veredicto (dump / niega / afirma);
- jobs ejecutados frente a fases planificadas;
- turnos hasta el cierre;
- veredicto del piloto frente a gold (ver A3).
Aceptación: reproduce la tabla de la sección 1 sobre el corpus.

**A3. Gold estructurado por caso.** Ampliar `tests/fixtures/l2_wave/sense_battery.json`: por caso, símbolos
que deben aparecer en la respuesta, polos que deben quedar negados y polos que deben quedar afirmados.
Reutilizar los golds de `tools/score_zone_judge.py` (`lsp_symbol_provider`, `busy_strip`).
Sustituir `ok_for_kind` de `score_sense_battery.py` (hoy solo mira `jobs_run` y jobs vacíos) por una
comprobación contra ese gold. El juicio libre del agente queda solo para el residuo.

**A4. Baseline de ruido.** Ejecutar la configuración congelada N=5 veces sobre los mismos casos, con
temperatura 0 y semilla si el servidor la admite. Publicar media y desviación por caso.
Regla de decisión: un cambio solo se acepta si mejora más que 1σ del baseline, o si mejora una métrica
de proceso de forma determinista.

**A5. Split entrenamiento / held-out.** Pasar de 5 a ≥ 20 casos usando los ya numerados en el repo
(por ejemplo 17, 20 y los de `tools/l2_explore_battery/`). Reservar ≥ 6 como held-out: el agente supervisor
solo ve el agregado, nunca las trazas.

**A6. Replay offline del piloto.** El harness ya tiene `wave-explore --script FILE`
(ver `tests/fixtures/l2_wave/script_escape_gap.txt`). Añadir un modo que reproduce los Cerrados grabados de
los jobs y ejecuta **solo el piloto y los validadores** sobre ellos. Un turno de piloto es pequeño, así que
la iteración de gates y esquema baja de una hora a minutos. También permite probar los validadores nuevos
contra las ~300 trazas grabadas sin llamar a ningún modelo.

### S0. Baseline mínimo (experimento sustractivo, antes de añadir nada)

Pregunta: ¿cuánto de la complejidad actual está pagando su coste?
- **Mínimo:** system prompt ≤ 1,5 KB (rol, tres o cuatro movimientos, formato JSON, «no leas código»); sin ningún flag `Sello*` ni `Gate*`;
  solo validación de legalidad del JSON y el tope de jobs. Mismo atlas, mismos hijos.
- **Actual:** configuración congelada de sense16.
- Protocolo A4 (N ≥ 5, mismos casos, gold de A3) en el modelo objetivo, y una pasada en T0 (sección 4).

Regla de decisión:
| Resultado | Decisión |
|---|---|
| mínimo ≈ actual (dentro de 1σ) | adoptar el mínimo; borrar los flags; el actual era ruido y coste |
| mínimo peor, pero solo en algunos casos | ablación por flag solo sobre esos casos; conservar lo que los arregle |
| mínimo claramente peor en todo | la complejidad paga; seguir con B–C para sustituirla por esquema |

Aceptación: tabla publicada con las tres columnas por caso. Es la decisión que gobierna el resto del plan.

### B. Contrato hijo → piloto

Cambiar el cierre del hijo (`jobs.md`, `brief.md`, `WaveDo::Cerrar`) a un objeto:
```json
{"veredicto":"hay|no_hay|no_concluyente",
 "simbolos":["path:Sym"],
 "evidencia":["path:l1-l2 una línea"],
 "falta":["concepto"]}
```
- Un `leído:` sin veredicto deja de aceptarse como cierre: se reintenta una vez y, si persiste, se marca `no_concluyente`.
- El piloto recibe `veredicto + simbolos + evidencia` (≈ 1–2 KB por job; cabe de sobra en su contexto).
- Flag: `kWaveJobVeredictoEstructurado`.
Aceptación (sobre replay + batería): Cerrados sin veredicto < 10 % (hoy 42 %), sin subir el % de `hay` falsos frente al gold.

### C. Cierre del piloto como tabla de polos

Sustituir el `why` libre del `cerrar` por:
```json
{"polos":[{"polo":"cursor","estado":"hay|no_hay|desconocido","evidencia":["path:Sym"]}],
 "unión":"hay|no_hay|no_verificada"}
```
Validación por esquema, no por regex:
- un `hay` debe citar símbolos presentes en el `visto` de al menos un job con veredicto `hay`;
- un `no_hay` debe apoyarse en un job con veredicto `no_hay`, o en ≥ 2 jobs `no_concluyente` con verbos distintos;
- la unión `hay` exige un job de puente (o de `verificar`) con veredicto `hay`.
Efecto: el veredicto se agrega sobre **todos** los jobs, no sobre el último. Deja de existir «el último Cerrado negó, no selles un polo anterior».

Retirada de gates: apagar por ablación los flags `kWaveControlSello*` y `kWaveControlGate*` (17 en `l2_wave.hpp`) de uno en uno
sobre el replay. Quitar los que no aporten con el esquema activo. Mantener `any_miss` solo si sigue siendo necesario.
Aceptación: turnos rechazados < 8 %; ningún deadlock de ≥ 3 rechazos seguidos en el corpus.

### D. Presupuesto y visibilidad de movimientos

- Sustituir `kWaveControlMaxJobs=3` por `max(3, fases del plan)` con techo absoluto (p. ej. 5) o por un presupuesto de tiempo/tokens.
  Objetivo: que el puente P se ejecute cuando el plan lo pide.
- Mostrar en cada turno la lista de `do` legales y el motivo del último rechazo, para no gastar turnos repitiendo un movimiento ilegal.
Aceptación: jobs ejecutados = fases planificadas en ≥ 90 % de los planes committed.

### E. Job `verificar` (condicional)

Solo si, tras B y C, los casos «existe» siguen cerrando la unión sin evidencia. Es una capacidad nueva: entra bajo la regla 6.

Nuevo tipo de job estrecho: recibe un símbolo ya visto y la afirmación del ancla; lee el cuerpo y devuelve
`hay|no_hay|no_concluyente` con líneas. Convierte los «dumps» (nombres sin comportamiento) en afirmaciones comprobadas
sin que el piloto lea código.
Aceptación: en los casos «existe», la unión se cierra con `verificar` en lugar de con texto libre.

### F. Pistas léxicas tras un miss (condicional)

Solo si tras B–C sigue habiendo consultas que repiten el verbo del falso amigo. Entra bajo la regla 6.

Tras un job `no_hay` o `no_concluyente`, el runtime ofrece al piloto un **menú** de verbos y sinónimos sacados
del índice (p. ej. `guardar → restaurar / cargar / reabrir`). El piloto elige; el runtime **no redacta la `consulta`**.
Ataca la trampa del tipo guardar/restaurar del caso 11 sin romper la prohibición existente.

### G. Dieta del prompt

- Reescribir el system prompt del piloto (~8 KB, ~40 reglas) a ~10 principios; el resto pasa a validación en código (fases B–C).
- Sustituir los few-shots del dominio spinner/cancelar IA por un dominio ajeno a las baterías.
- Ablación por regla sobre el replay antes de borrar nada.
Aceptación: prompt ≤ 3 KB, sin caída frente al baseline A4.

### H. Escalera de modelos (ver sección 4)

## 4. Protocolo de escalera de modelos

Objetivo: iterar más rápido sin que el resultado dependa de la potencia del modelo.

| Nivel | Modelo | Para qué sirve | Qué NO prueba |
|---|---|---|---|
| T0 techo | modelo grande (API) | Detectar fallos del harness, del atlas o del gold | Que el modelo local funcione |
| T1 medio | Haiku 4.5 o un local grande | Regresión rápida | Comportamiento del objetivo |
| T2 objetivo | el que se distribuye (hoy `qwen2.5-coder-14b` vía `api_base`) | **Única evidencia de mejora** | — |

Reglas:
1. **Inferencia asimétrica.** Si T0 falla un caso, el problema es del harness, del atlas o del gold: señal fuerte. Si T0 pasa, no dice nada sobre T2.
2. **Métrica principal = brecha** `pass(T0) − pass(T2)` por caso. Un cambio es bueno si sube T2 con T0 constante o si reduce la brecha. Subir T0 y T2 a la vez no prueba nada sobre el harness.
3. **Mismo texto, sin herramientas.** T0 recibe exactamente el system/user del piloto y devuelve el mismo JSON. No usar un agente con acceso al repo (Cursor, Claude Code) como «modelo»: podría leer código y saltarse el aislamiento que se quiere probar.
4. **Ablación por rol.** Piloto grande + hijos locales frente a piloto local + hijos grandes. Localiza qué rol es el cuello de botella. El contexto del piloto es pequeño (~4 k tokens), así que dárselo a un modelo grande es barato.
5. **Uso en el bucle.** T0 para depurar esquema, validadores, topes y deadlocks (fases B–D). T2 para aceptar o rechazar. Ningún cambio se fusiona sin T2 con repeticiones (A4).
6. Comprobar antes si el cliente de `l2_brain_remote` admite el proveedor elegido: hoy habla OpenAI-compatible (`ai.level2.api_base`); si no, hace falta un proxy o adaptador.

## 5. Riesgos

- **Sobreajuste al conjunto de 5 casos.** Mitigación: A5.
- **Esquema demasiado rígido para el modelo pequeño.** Mitigación: un reintento con el error de validación y `no_concluyente` como salida legal.
- **Regresión al quitar gates.** Mitigación: ablación sobre el replay antes de tocar la batería real.
- **Confundir potencia del modelo con calidad del harness.** Mitigación: sección 4.
- **Corpus obsoleto tras cambiar el formato de cierre.** Mitigación: `MANIFEST.json` con commit y flags; regenerar tras B y C.

## 6. Qué NO hacer

- Dejar que el piloto lea código.
- Que el runtime redacte la `consulta`.
- Recortar `do` o el mazo para clavar un test.
- Añadir más `kWaveControl*` mientras no exista el baseline de ruido (A4).
- Declarar «criterio cumplido» con una sola ronda.

## 7. Checklist

- [x] A1 corpus archivado y cargable
- [x] A2 `process_metrics.py` reproduce la tabla base
- [x] A3 gold estructurado y scorer nuevo
- [x] A4 baseline de ruido publicado
- [x] A5 ≥ 20 casos, ≥ 6 held-out (`sense_battery_a5.json` + split; core5 intacto)
- [x] A6 replay offline del piloto
- [x] S0 baseline mínimo frente a actual (decide el resto) → adoptar mínimo
- [x] B veredicto estructurado del hijo
- [x] C cierre por tabla de polos + ablación de gates
- [x] D tope de jobs escalado + movimientos legales visibles
- [ ] E job `verificar` (solo si hace falta)
- [ ] F menú de pistas léxicas (solo si hace falta)
- [x] G prompt ≤ 3 KB, few-shots fuera de dominio — **rechazado en T2** (0.04≪S0 0.44); few-shots revertidos a S0
- [ ] H brecha T0/T2 medida por caso
