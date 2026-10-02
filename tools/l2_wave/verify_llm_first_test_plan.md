# Plan de testeos: verificador LLM-first (menos runtime)

Objetivo: que el **veredicto lo ponga el modelo**; el runtime solo
**rechaza forma**, **reintenta** o **ofrece tools/evidencia**. No usar
M2/M3 (downgrade `sostiene`→`dudoso` en runtime) como solución de producto.

Caso ancla: `04_deseo_console_gutter` congelado
(`.tuide/ai/l2_admin_probe/battery20_full_20260924_214909/04/...`).
Gold: **bloquear** (`refuta`|`dudoso`). Control negativo opcional: un
`existe` (01) no debe degradarse a bloqueo sistemático.

Harness: sintético **solo verificador** (reusar jobs/examen; no re-explorar).
Script previsto: extender `verify_measures_synth.py` o
`verify_llm_first_synth.py`.

---

## Principio

| Rol | Permitido | Prohibido (en este plan) |
|-----|-----------|---------------------------|
| Runtime | Rechazar JSON mal formado; pedir reemisión; inyectar hechos tipados; correr tools | Cambiar `veredicto` del modelo |
| LLM | Emitir `sostiene\|refuta\|dudoso` + `cobertura` | — |

M2/M3 quedan como **baseline de control** en el harness (etiqueta
`CONTROL_runtime_*`), no como candidato a merge.

---

## Aproximaciones a probar (aisladas)

### A — Contrato de cierre con anclas a jobs (forma + reintento)

**Idea:** al `cerrar`, cada ítem de `cobertura` exige:
`{"id","estado","job_ids":[…],"anclas":[…]}`.

Runtime **no** cambia el veredicto. Si `estado=cubierto` y
(`job_ids` vacío **o** todos esos jobs son miss), **rechaza** y pide
reemitir una vez (como “cerrar exige why”).

**Test:** A0 vs A1 (sin / con contrato).  
**Éxito:** el modelo, tras el rechazo, baja a `hueco`/`dudoso`/`refuta`
**sin** downgrade automático.  
**Fallo:** insiste en `sostiene`+cubierto ilegal tras el reintento →
anotar (entonces hace falta B o C, no M2).

### B — Contra-pregunta tras `sostiene` (sigue LLM)

**Idea:** si cierra `sostiene` y el notebook tiene miss tipados, un
turno de usuario fijo lista esos jobs y exige: o rebajar cobertura, o
explicar por qué el miss no toca el examen; reemitir `cerrar`.

Runtime no pisa el veredicto; solo añade el turno.

**Test:** B solo; B+A.  
**Éxito:** segundo cierre bloquea (modelo).  
**Riesgo:** ignora la contra-pregunta → medir tasa.

### C — Evidencia de veredictos + obligación de citarlos

**Idea:** M1 mejorado: tabla `job→veredicto` en contexto **y** system:
“al marcar cubierto debes citar `job_ids` de jobs no-miss”.

Sin rechazo runtime (puro prompt) vs con rechazo A.

**Test:** C solo (¿M1 falló porque no obligaba citar?).  
**Éxito:** bloqueo espontáneo del modelo.  
**Si falla:** confirma que hace falta A (forma), no más prosa.

### D — Tool `entre` dirigida miss↔encontrado

**Idea:** antes o durante fase 2, 1–2 `entre` automáticos **como
resultado de tool** (el runtime ejecuta, el LLM lee): símbolos/path del
job miss vs del job encontrado. Sin camino = texto “sin co-ocurrencia”.

El LLM decide; runtime no downgradéa.

**Test:** D solo; D+A.  
**Éxito:** el modelo marca hueco en el arco miss.  
**Nota:** la tool es ejecución mecánica; la decisión sigue siendo LLM.

### E — Fase 2 en dos pases (ciega al “primo”)

**Idea:** pase 2a: solo anclas/veredictos de jobs miss/parcial.  
Pase 2b: añadir jobs encontrado. Cierre solo al final.

**Test:** E vs baseline.  
**Éxito:** tras 2a ya no puede fingir e1 cubierto; 2b no absuelve solo.

### F — (Opcional, coste×2) Refutador separado

**Idea:** tras un `sostiene`, un segundo system “solo refuta cada
cubierto”. Runtime agrega: si el refutador emite `refuta` en algún
slot → el gate bloquea **usando el veredicto del refutador** (sigue
siendo LLM, no heurística de dominio).

**Test:** solo si A–E no bastan.  
**Éxito:** refutador pilla el falso cubierto en 04.

### CONTROL — M2 / M3 / M4

Correr en la misma batería para comparar, etiquetados
`CONTROL_runtime_miss` / `CONTROL_runtime_all_covered` /
`CONTROL_skip_omit`. No son el objetivo del plan.

---

## Matriz de corridas (fase 1: solo caso 04)

| ID | Setup | LLM decide | Runtime |
|----|--------|------------|---------|
| R0 | `exam_hard` baseline | sí | gaps ya existentes |
| R1 | A contrato job_ids | sí (+reintento) | rechazo forma |
| R2 | B contra-pregunta | sí | turno extra |
| R3 | C citar veredictos | sí | nada / o A |
| R4 | D entre miss↔hit | sí | tool result |
| R5 | E dos pases | sí | orden de contexto |
| R6 | A+B | sí | forma + turno |
| R7 | A+D | sí | forma + tool |
| C1–C3 | M2, M3, M4 | no / política | control |

Cada celda: 1 corrida (o N=2 si hay ruido). Guardar
`veredicto_modelo`, `veredicto_final`, `reintentos`, log, `COMPARE.md`.

---

## Fase 2 (si algo gana en 04)

Repetir **ganador(es)** en sintético:

1. **06** (misma trampa, sin miss temprano — ¿A/B/D siguen ayudando?).
2. **09** solo para **M4/skip** (omit ≠ sostienen) + comprobar que A no rompe el `refuta` bueno.
3. **01 existe** (no debe bloquear de más).

Luego, si sigue en pie: 1 probe E2E corto (piloto+verify) en 04, no batería 20.

---

## Criterios de éxito

**Prometedora (merge-candidata):**

- En 04: bloquea con `veredicto_modelo` ∈ {refuta, dudoso} **o** tras
  reintento forma el modelo corrige sin downgrade.
- En 01: no introduce bloqueo sistemático.
- No añade pistas de dominio (consola/gfortran) en prompts/vetos.

**Descartar:**

- Solo funciona vía CONTROL M2/M3.
- Sube falsos bloqueos en existe.
- Requiere few-shots del caso.

**Orden de preferencia si empatan:** A ≥ B ≥ D ≥ E ≥ C ≥ F ≥ CONTROLs.

---

## Qué no hacer en este plan

- Lista de false friends en runtime.
- Personalizar examen/few-shots a 04/06/09.
- Batería de 20 hasta tener 1–2 medidas ganadoras en sintético.
- Sustituir el verificador por M2/M3 en producto.

---

## Entregables

1. Harness `verify_llm_first_synth.py` (flags R0–R7 + controls).
2. `COMPARE.md` por corrida + tabla resumen.
3. Nota corta: ganador, por qué es general, riesgos residuales.
4. (Solo entonces) cablear ganador en `probe_admin_pilot` / `l2_admin` y
   re-probar 04 E2E.
