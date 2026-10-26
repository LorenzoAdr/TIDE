# Plan: harness de código v2 — de "investigador honesto" a agente de codificación de primera línea

> **Estado:** propuesta (sin implementar). Fecha: 2026-10-25.
> **Sustituye** el encuadre del análisis previo (borrado): sobrevaloraba el gate
> epistémico, las tools exóticas del explorador y los embeddings como ventajas
> competitivas. Este documento los reclasifica y plantea el plan real.
> **Alcance:** ruta de producto `admin_v1` (`src/ai/l2_admin.*`,
> `src/ai/ai_controller.cpp`), `ToolRegistry` (`src/ai/tool_registry.cpp`),
> transporte de modelo (`src/ai/l2_brain*.cpp`).
> **Cifras:** batería propia `admin_vibecode_100.json` (100 casos,
> `qwen3.6-27b-q8_0`, ~19 h) según [`l2-admin-verify-round-reduction.md`](l2-admin-verify-round-reduction.md) §5-§6.
> No hay medidas nuevas en este documento.

---

## 1. Diagnóstico corregido

### 1.1 Lo que creí ventaja y no lo es

| Pieza | Por qué no es ventaja | Evidencia |
|---|---|---|
| **Gate epistémico** (verificador + refutador) | Es un **seguro tarifado para modelos débiles**. Con modelo fuerte la alucinación que pretende cazar es rara, y el peaje se paga siempre. En el camino limpio no aporta señal: 1 job `encontrado` → `sostiene` **7/7** y el refutador **0/7 flips**. En el resto se convierte en fricción: **73,6 % refuta, 38 % de casos agotan el tope, 56 % acaban en `ask_user`** (≈61 % de ellos evitables según tu propio análisis manual). | §5-§6 de `l2-admin-verify-round-reduction.md` |
| **`trail` / `dataflow`** como tools del agente | **El modelo no las elige nunca.** Hubo que escribir enrutado heurístico a mano para forzarlas, y el propio comentario del código lo admite: *"visto en producción: tools configuradas y ofrecidas en el prompt, pero jamás elegidas"*. Un modelo fuerte resuelve lo mismo con `grep`+`read` y menos ceremonia. | `l2_admin.cpp:1583-1586`, commit `8187689` |
| **Embeddings / rerank** | **Ya están muertos en la ruta de producto.** `coding_embed_rerank` (1951), `coding_stem_embed_index` (948), `coding_symbol_embed_index` (396), `embedding_backend` (1196) ≈ **5.300 líneas** que solo usan L0/L1 legacy; con `admin_enabled=true` el panel AI no arranca embeds. Se compilan en el binario y no se ejecutan. | `grep` de usuarios reales; `docs/ai/l2-admin.md` ("abrir el panel AI no arranca mapa de símbolos ni embeds") |
| **"Batería propia" como foso** | Mide el harness **de Python**, no el binario: `explore_lite_local.py`/`verify_lite_local.py`/`probe_admin_pilot.py` reimplementan la lógica a mano, con divergencia ya documentada y dieta *explore-only*. Y 19 h por tirada = no se itera, se adivina. | §0bis del mismo doc |

**Reclasificación:** nada de esto se borra por decreto, pero **ninguno es un eje
donde competir**. El gate y los veredictos tipados pasan a ser **una política
opcional del perfil "búnker"**; `trail`/`dataflow` pasan a ser **features de IDE
para humanos** (ya tienes la vista de causal-flow: `cb0bf60`, `6e09274`); los
embeddings salen del agente (como mucho, sobreviven para una feature de
*búsqueda semántica* del IDE, no para el harness).

### 1.2 Lo que sí es ventaja real (y está sin explotar)

No es la astucia del agente: es el **sustrato de IDE** debajo, que ningún
harness de terminal tiene.

| Ventaja | Estado hoy | Por qué nadie la copia |
|---|---|---|
| **Diagnostics de clangd en caliente** | tool `diagnostics` existe, **no se realimenta tras editar** | opencode tiene `lsp` experimental; qwen-code y aider no tienen LSP |
| **Code actions / quick-fixes de clangd** | ❌ no expuesto | nadie lo expone como tool |
| **Build/test runner con whitelist y Docker** | ✅ `TaskRunner`, pero el agente no está obligado a usarlo tras editar | los demás tiran de `bash` a ciegas |
| **DAP: ejecutar, breakpoint, inspeccionar valores reales** | ✅ el IDE lo tiene, ❌ el agente no lo puede tocar | **imposible para un CLI**: requiere un depurador integrado |
| **Core dumps / forense de binarios (`nm`, Core Analyzer)** | ✅ en el IDE, ❌ invisible al agente | idem |
| **Índice de símbolos + Tree-sitter + `get_code_of`** | ✅ y es bueno (extracción de cuerpo por símbolo) | aider tiene repo map; nadie tiene extracción estructural por símbolo |
| **Perfil búnker: binario único, offline, GBNF local** | ✅ único | ningún harness JS/Python empaqueta modelo + LSP + depurador |
| **Suite de trampas `false_friend`** | ✅ 20/20 | convertirla en **test de regresión**, no en impuesto de runtime |

**Reencuadre del foso:** cambiar **"verdad por argumentación LLM"** por
**"verdad por oráculo mecánico"**: compila, los tests pasan, clangd no se queja,
el depurador dice que la variable vale X. Eso es más barato, más fiable, no
depende de que el modelo sea débil, y **solo lo puede hacer un IDE**.

### 1.3 Dónde estamos realmente detrás

Esto no cambia respecto al análisis anterior — y es la parte que tú ya validaste.

**Fontanería** (`l2_brain_remote.cpp`): `curl` por `popen` con fichero temporal
por llamada · **sin streaming** (UI muda hasta 300 s) · sin reintentos ni
clasificación 429/5xx · **`grammar_file` ignorado en remoto** y
`ADMIN_JSON_GRAMMAR` apagado por defecto (`l2_grammar.cpp:21` +
`features_promoted.json`) → el hot path corre **sin salida estructurada** · sin
leer `usage` (0 referencias a tokens/coste) · **0 llamadas a `ai_trace` en
`l2_admin.cpp`** · un solo modelo para los 4 roles (`ai_controller.cpp:1023`).

**Contrato de agente**: sin tool-calling nativo (JSON en texto) · sin historial
de mensajes (prompt re-renderizado desde estado) → sin caché de prefijo ni
continuidad · 1 spawn por turno, cero paralelismo.

**Agencia de edición**: **no puede crear archivos** (`search_replace.cpp:577`) ·
un hunk por llamada LLM, un archivo por `spawn` · **sin gate de compilación tras
editar** en `admin_v1` (el autofix con rollback vive en el legado) · sin
revisión humana del diff · `edit_journal`/gutter nunca cableado a admin ·
escribe a disco por detrás del `EditorBuffer`. Resultado: **11 ediciones en 100
casos**.

**Contexto**: `n_ctx_remote=32768` por defecto y el explorador ve ≈**9 KB de
código** por investigación (≤4 greps + ≤3 reads × 3000 chars). Recortes fijos,
no función del contexto del modelo.

**Contrato de proyecto**: **no se ingiere ningún `AGENTS.md`** (0 referencias).
El agente no sabe cómo compilas, cómo testeas, ni qué no tocar.

**Lastre**: ~**40.000 líneas** de `src/ai/` se compilan en `tuide` sin ser
alcanzables desde la ruta admin (`l2_effect_registry` 11.346,
`level2_session` 8.893, `l2_effect_slice` 3.760, `l2_explore_a` 3.253,
`level2_autonomous_loop` 2.461, `l2_effect_summary` 1.884, `level1_agent` 1.890,
embeddings 5.300, …). Cada cambio en el harness paga ese peaje.

---

## 2. Objetivo y arquitectura destino

**Objetivo:** paridad total con opencode/qwen-code/Claude Code en contrato de
agente, tool belt y UX; y superioridad en **verificación mecánica** gracias al
IDE. El perfil búnker (modelo local pequeño) se conserva como capacidad
diferencial, no como el diseño por defecto.

```
┌──────────────────────────────────────────────────────────────┐
│ AgentSession (historial real de mensajes, persistido)        │
│  system | user | assistant(+tool_calls) | tool               │
│  compactación al 70% · resume · branch · cancelación         │
└───────────────┬──────────────────────────────┬───────────────┘
                │                              │
      ┌─────────▼─────────┐          ┌─────────▼──────────┐
      │ LlmClient          │          │ ToolBelt           │
      │ SSE streaming      │          │ read/write/edit    │
      │ tool_calling nativo│          │ grep/glob/ls       │
      │ json_schema / GBNF │          │ bash (3 tiers)     │
      │ retries + usage    │          │ lsp_* · tasks      │
      │ caps negotiation   │          │ todo · task · ask  │
      └─────────┬──────────┘          └─────────┬──────────┘
                │                               │
      ┌─────────▼───────────────────────────────▼──────────┐
      │ Policy (por perfil)                                │
      │  strong : gates mecánicos, verificador OFF         │
      │  bunker : admin_v1 JSON + presupuestos + gate ON   │
      └─────────┬──────────────────────────────────────────┘
                │
      ┌─────────▼──────────────────────────────────────────┐
      │ VerifyPipeline (el oráculo, no el LLM)             │
      │  format → lsp diagnostics → compile → test         │
      │  ≤N autofix · rollback a snapshot · diff review    │
      └────────────────────────────────────────────────────┘
```

Dos perfiles, un solo loop y un solo tool belt:

| | `strong` (default) | `bunker` |
|---|---|---|
| Disparador | caps: tool-calling + contexto ≥32 K | local pequeño / sin tool-calling |
| Contrato | mensajes + tool_calls nativos | `admin_v1` JSON + GBNF |
| Contexto | archivos completos, presupuesto por `n_ctx` | recortes actuales |
| Verificador LLM | **off** (`on_close` opcional) | **on** |
| Explorador hijo | `task` genérico, opt-in | explorador lite obligatorio |
| Oráculo | compile + test + diagnostics | compile + verificador |

---

## 3. Plan por hitos

Dependencias duras: **M0 → M1 → M2 → M3**. M4/M5/M6 se intercalan. M7 después de M3.
Estimaciones en semanas de trabajo enfocado (1 persona + asistencia IA).

### M0 — Transporte de modelo · ✅ IMPLEMENTADO (2026-10-25)

> **Estado:** código en árbol, sin commitear. **Binario `tuide` compilado sin
> errores ni warnings**; `llm_client_test` (93 asserts), `l2_admin_test`,
> `search_replace_test` y `edit_snapshot_test` en verde. Smoke de transporte
> contra servidor SSE falso en verde (21 comprobaciones): reintento 429 +
> `Retry-After`, SSE reensamblado con línea partida entre chunks HTTP, **4
> peticiones sobre 1 conexión TCP**, fallback en ambos sentidos (stream↔entero),
> camino `curl --config` con su stderr en el diagnóstico, y temporales 0600 sin
> residuos.
>
> **Revisión posterior (misma fecha) — 5 defectos encontrados y corregidos:**
>
> | # | Defecto | Impacto |
> |---|---|---|
> | 1 | El `break` por socket keep-alive rancio no reintentaba: caía al código de abajo y devolvía `BadResponse "HTTP 0"` | un socket en reposo cerrado por el otro extremo rompía la consulta en vez de reconectar |
> | 2 | Una respuesta *enmarcada* (chunked/Content-Length) cortada a medias se devolvía como `ok` | JSON truncado tratado como válido; ahora `BadResponse` + se distingue del caso legítimo "sin longitud, cierra = fin" (`HeadRouter::framed/note_eof`) |
> | 3 | El stderr de `curl` se perdía (iba al mismo pipe que el cuerpo, dentro del router) | un fallo de DNS/TLS se veía como `curl exit=6` sin motivo; ahora va a su propio fichero y al mensaje. Además deja de poder intercalarse en mitad del SSE |
> | 4 | `last_attempts_text()` compartía mutex con `chat()` | leerlo desde la UI habría congelado el IDE hasta 300 s; mutex propio |
> | 5 | El explorador armaba su propio `AdminLoopOpts` sin los callbacks nuevos | ≈1 de cada 4 llamadas LLM no se contaba ni se transmitía al panel; ahora los comparten piloto y explorador |
>
> Extras de la revisión: clave de API **fuera de `argv`** (fichero `--config`
> 0600 — antes era visible en `ps` para cualquier usuario de la máquina, igual
> que en el camino viejo), temporales creados con `O_EXCL|0600`, y documentado
> el acoplamiento `response_format:json_object` ↔ la palabra "json" en el prompt
> (OpenAI/DeepSeek devuelven 400 sin ella; los 4 prompts la llevan).
>
> **Nota sobre la suite:** 11 tests fallaban ya antes de M0 (`bracket_match`,
> `text_ops`, `tree_sitter`, `git_diff`, `csv_index`, `nm_reader`,
> `tabular_file`, `l2_trail_build`, `l2_wave`, `level2_session`,
> `level2_autonomous_loop`). Ninguno enlaza un `.cpp` tocado por M0, y los dos
> legacy fallan igual con el `features_promoted.json` de HEAD — son deuda
> anterior, no regresión.
>
> | Entregable | Dónde |
> |---|---|
> | `LlmClient` + SSE + reintentos + usage + caps | `src/ai/llm_client.{hpp,cpp}` (nuevo, ~1.200 líneas) |
> | Transporte socket keep-alive (http://) y curl (https://) | ídem, `socket_post` / `curl_post` |
> | Test sin red | `tests/llm_client_test.cpp` + target en `CMakeLists.txt` |
> | Caps por proveedor + rol | `src/ai/ai_types.hpp` (`AiProviderCaps`, `AiRoleBackend`, `ai_resolve_role_backend`) |
> | Config `ai.caps.*` / `ai.roles.*` (lectura y escritura) | `src/app/workspace_config.cpp` |
> | `RemoteL2Brain` sobre el cliente nuevo | `src/ai/l2_brain_remote.cpp` (reescrito) |
> | Streaming al panel + telemetría | `src/ai/ai_controller.{hpp,cpp}`, `src/ai/l2_admin.{hpp,cpp}` (`admin_brain_call`) |
> | Prompt reordenado para caché de prefijo | `admin_user_prompt` en `src/ai/l2_admin.cpp` |
> | GBNF del piloto activado | `tools/l2_battery/features_promoted.json` |

> **Validado en vivo (2026-10-26)** contra el host real
> (`qwen3.6-27b-q8_0` vía `host_llama_spy` → llama-server), tras arreglar el
> proxy:
>
> | Medida | Antes de M0 | Ahora |
> |---|---|---|
> | Eventos SSE en una generación de 48 tokens | 2 (todo al final) | **49**, 47 deltas de contenido |
> | Primer delta | al 99 % del total (424/428 ms) | **al 12 %** (616 ms de 5342) |
> | Cadencia de llegada | ráfaga única | un delta cada ~100 ms, el ritmo real del modelo |
> | `usage` | ausente | `prompt_tokens` / `completion_tokens` reales |
> | Caché de prefijo | no medible | **explorer 55 %, pilot 47 %** de los tokens de entrada |
> | Rechazos por formato | 28/100 casos (batería) | **0** en las tres consultas reales |
> | Reintentos de transporte | n/a | 0 |
>
> Primera medición real del lazo de producción (consulta
> "explícame cómo el explorador decide entre grep y read", veredicto
> `sostiene`, respuesta correcta citando el bloque de routing):
>
> ```json
> {"llm_calls": 8, "proposes": 3, "spawns": 1, "transport_retries": 0,
>  "roles": {
>    "explorer": {"calls": 5, "prompt_tokens": 14239, "cached_prompt_tokens": 7830,
>                 "completion_tokens": 441, "ms": 110536},
>    "pilot":    {"calls": 3, "prompt_tokens": 4978,  "cached_prompt_tokens": 2341,
>                 "completion_tokens": 850, "ms": 116707}}}
> ```
>
> Lo que estos números deciden, y antes no se podía argumentar:
> **el explorador se come 110 s de los 227 s en 5 llamadas que solo eligen
> "grep esto / lee aquí"**. Y el 47 % de caché del piloto es la línea base
> contra la que medir cualquier cambio futuro del prompt.
>
> **Corrección medida (no asumir que un explorador pequeño sale gratis).** El
> enrutado por rol funciona —`ai.roles.explorer` apuntado a otro endpoint bajó
> el coste del explorador de 110 s a **0,37 s en 4 llamadas**— pero con un
> explorador *malo* (stub con respuesta enlatada) la consulta entera salió
> **peor**: el piloto pasó de 3 a 10 turnos y de 1 a 6 spawns, y entró el
> verificador. Total ~454 s frente a 227 s.
>
> Es decir: **la calidad del explorador fija el número de rondas del piloto**,
> así que un modelo pequeño ahí solo gana si cierra la caza. El criterio no es
> "pon el explorador en el modelo más barato" sino "baja de modelo mientras el
> veredicto siga siendo `encontrado` con evidencia; si empieza a devolver
> `no_encontrado`/`parcial`, lo que ahorras en el hijo lo pagas multiplicado en
> el piloto". Medible ya con la telemetría de M0: `roles.explorer.calls` y
> `proposes` en el mismo `llm_calls.json`.
>
> Un endpoint de rol caído no rompe la sesión: `ensure_ready` lo sondea con un
> socket, avisa (`L2 rol explorer: … no responde — ese rol usa el modelo base`)
> y retira el override.
>
> **APARCADO: medir un explorador pequeño de verdad.** El host sirve un solo
> modelo a la vez (27B) y no hay GGUF pequeño levantado, así que la comparación
> real queda pendiente. Cuando haya uno, es un comando y una consulta:
>
> ```bash
> # en el host (puertos propios; el backend se deriva a CHAT_PORT+10000)
> TUIDE_HOST_CHAT_PORT=8081 ./tools/run_host_llama.sh --llm 7b \
>   --no-embed --no-web --foreground
> ```
>
> ```json
> // .tuide/config.json → ai.roles
> {"explorer": {"api_base": "http://192.168.64.1:8081/v1",
>               "api_model": "qwen2.5-7b-instruct-q4_k_m"}}
> ```
>
> Y comparar en el mismo `llm_calls.json` de `l2_harness_cli admin-run`:
> `roles.explorer.ms` (lo que ahorras) contra `proposes` y `spawns` (lo que
> puedes pagar arriba). Candidato recomendado el 7B, no el 1.5B/3B, por la
> corrección de arriba.

Plan original del hito, para referencia:


| # | Trabajo | Archivos | Aceptación |
|---|---|---|---|
| 0.1 | `LlmClient` nuevo: `chat(messages, tools, opts, on_delta) → {content, tool_calls, usage, finish_reason, retries}`. HTTP persistente (libcurl easy reutilizado o cliente propio), **SSE**, reintentos con backoff jitter en 429/5xx/timeout, timeout por rol, cancelación cooperativa | nuevo `src/ai/llm_client.{hpp,cpp}`; `l2_brain*.cpp` pasa a adaptador fino | una consulta sobrevive un 429 y un corte de socket sin abortar; sin ficheros temporales ni `popen` |
| 0.2 | **Streaming al panel AI** coalescido por `UI_WAKE` (respetando `UiActivityGate`) | `ai_controller.cpp`, `src/ui/console_panel.cpp` | se ven tokens mientras el modelo piensa; cancelar con ESC corta en <200 ms |
| 0.3 | `ProviderCaps {tool_calling, json_schema, streaming, prompt_cache, parallel_tools, context_window}` por backend, desde config con defaults sensatos | `ai_types.hpp`, `llm_client.cpp` | `/model` imprime las caps efectivas |
| 0.4 | **Salida estructurada real**: `response_format: {type: json_schema}` cuando caps lo permitan; `grammar`/`json_schema` en body para `llama-server`; `ADMIN_JSON_GRAMMAR=true` en `features_promoted.json` | `llm_client.cpp`, `l2_grammar.cpp`, `features_promoted.json` | **0 reintentos por "JSON inválido"** en una tirada de 20 casos |
| 0.5 | `usage` → agregado por rol y por consulta; footer del panel con tokens y coste estimado; `ai_trace` por llamada (rol, tokens, ms, retries, finish_reason) + registro resumen por consulta | `llm_client.cpp`, `ai_controller.cpp`, `l2_admin.cpp` (hoy 0 `ai_trace`) | `/trace tail` responde "¿en qué se fueron las rondas y los tokens?" sin lanzar la batería |
| 0.6 | Orden del prompt para caché de prefijo: inmutable (system + root + AGENTS.md) → estable (resumen/notebook) → volátil (presupuesto, `do` legales, último rechazo). `cache_control` donde el proveedor lo soporte | `admin_user_prompt` (`l2_admin.cpp:4923`) | el proveedor reporta hits de caché en turnos ≥2 |
| 0.7 | **Modelo por rol**: `ai.models.{pilot,explorer,verifier,edit}` con fallback al global | `ai_types.hpp`, `ai_controller.cpp:1023` | explorador en modelo barato y piloto en el fuerte, misma consulta |

**Por qué primero:** elimina los ~28 reintentos de formato y los ~15 de
confirmación ilegal medidos, baja el 54 % de llamadas que se come el piloto, y
es la única forma de **medir en producción** en vez de en la batería.

### M1 — Loop de agente v2 (≈2,5 semanas)

| # | Trabajo | Aceptación |
|---|---|---|
| 1.1 | `AgentSession` con historial real de mensajes (`system/user/assistant+tool_calls/tool`), persistido append-only en `.tuide/ai/sessions/<id>.jsonl` | matar la app y reanudar la misma sesión con el historial intacto |
| 1.2 | Bucle de tool-calls nativo: despacho por nombre+args JSON, resultado como mensaje `tool`, iteración hasta `finish_reason=stop` o tope de turnos | una tarea de 15 tool-calls corre sin un solo "JSON inválido" |
| 1.3 | **Ejecución paralela** de tools de solo lectura (`grep`/`read`/`glob`/`lsp_*`) en pool, resultados en orden determinista | 4 greps en un turno tardan ~1 grep |
| 1.4 | **Compactación** al 70 % del contexto: resumen de turnos antiguos en bloque de "hechos + archivos tocados + decisiones" (reusar el **formato** del notebook, que es su verdadero valor) + últimos N turnos verbatim | una sesión de 200 K tokens no se cae ni pierde el hilo |
| 1.5 | Perfiles `strong`/`bunker` seleccionados por caps, con override en settings; `admin_v1` sobrevive **íntegro** como política del perfil búnker | misma consulta corre en los dos perfiles con distinto número de llamadas y mismo resultado |
| 1.6 | Interrupción y re-dirección: el usuario escribe mientras corre y el mensaje entra en el historial en el siguiente turno | se puede corregir el rumbo sin cancelar |
| 1.7 | Tope de turnos alto y honesto (p.ej. 60) con aviso y continuación opt-in, en lugar de los presupuestos duros actuales (solo búnker los mantiene) | una tarea de edición multi-archivo no muere por `proposes≥12` |

### M2 — Tool belt (≈2 semanas, solapa con M1)

**Nuevas de escritura** (hoy no existe ninguna):

| Tool | Contrato | Notas |
|---|---|---|
| `write_file` | `{path, content}` crea/sobrescribe | perímetro + snapshot + confirmación según política |
| `edit` | `{path, hunks:[{search,replace}]}` | **multi-hunk**, atómico por archivo; reusa `find_unique_span_allow_flex` |
| `multi_edit` | `{edits:[{path,hunks}]}` | atómico entre archivos: todo o nada, snapshot previo |
| `apply_patch` | diff unificado | para cambios grandes/renombres |
| `move_file` / `delete_file` | gated | detrás de confirmación |
| `format_file` | clang-format del repo | determinista, barato |
| `git_commit` / `git_branch` | gated, off por defecto | cierra el ciclo tras gate verde |

**Arreglos en las de lectura:**

- `read_file` con `offset`/`limit` y **sin recorte agresivo por defecto** (lo
  decide el presupuesto, ver 2.5) — hoy head 2800 + tail 2800 y luego 3000/1800.
- `grep`: pasar `rg --json` con líneas de contexto y tope por presupuesto, no 1200 chars / 8 hechos.
- `glob` (falta), `list` (existe como `list_files`).
- `lsp_diagnostics` por archivo con espera a `publishDiagnostics` + timeout.
- **`lsp_code_actions` + `apply_code_action`** (nuevo): quick-fixes de clangd aplicados sin gastar un token de generación.
- Conservar `get_code_of`, `repo_map`, `file_outline`, `workspace_symbols`, `definition`, `references`, `hover`.

**Ejecución:** `bash` completo con el gate de 3 niveles ya existente
(`admin_shell_cmd_gate`) + ruta Docker + salida en streaming + jobs en
background; `run_task` (compile/test/launch).

**De agente:** `todo_write` (estado de plan — retira los parches de "nudge" del
prompt: `cc0ce2a`, `bb6b659`), `task` (subagente **genérico** con allowlist de
tools; el explorador lite queda como *un preset*, no como el único hijo),
`ask_user` (se mantiene, funciona), `web_search`/`web_fetch` (se mantienen).

**Degradaciones y bajas:**

| Pieza | Acción |
|---|---|
| `trail`, `dataflow` | **off por defecto** como tools de agente; promover a comandos de IDE para humanos (ya hay vista causal-flow) |
| `headers_of` | se queda, es barato |
| `context_pack`, `apply_demo` | borrar (artefactos de Fase A/D) |
| `coding_embed_*`, `embedding_backend` | **fuera del agente**; decidir aparte si sobreviven para una feature de búsqueda semántica del IDE |

**2.5 — Presupuesto de contexto proporcional al modelo:**

| Perfil | Disparador | `read` | greps | reads |
|---|---|---|---|---|
| `tiny` | `n_ctx ≤ 8 K` | head+tail 2800 (hoy) | 4 | 3 |
| `mid` | `32 K` | archivo completo ≤1500 líneas | 10 | 8 |
| `large` | `≥128 K` | archivo completo + vecinos | sin tope práctico | sin tope práctico |

Más caché de ficheros por sesión (clave `path+mtime`): nunca reenviar lo ya
enviado, referenciarlo.

### M3 — Verificación mecánica (≈1,5 semanas) · *el cambio de foso*

| # | Trabajo | Aceptación |
|---|---|---|
| 3.1 | `VerifyPipeline` tras **cada lote de edición**: `format` → `lsp_diagnostics` de los archivos tocados → task `compile` → task `test` (si existe) → resultado estructurado (`file:line`, mensaje, diff vs baseline) devuelto como mensaje `tool` | ningún cierre tras editar sin compilación verde o rollback explícito |
| 3.2 | Autofix acotado: ≤3 iteraciones, rollback al snapshot sombra, feedback old/new. **Portar** desde `level2_autonomous_loop.cpp` | un edit que rompe el build se arregla o se revierte, nunca se queda roto |
| 3.3 | **Revisión humana del diff**: lista de hunks en el panel AI con accept/reject/accept-all; `ai.edit.auto_accept` opt-in | el usuario ve qué va a cambiar antes de que cambie |
| 3.4 | Aplicar por `EditorBuffer` cuando el archivo está abierto + `edit_journal` + gutter azul (master-spec §4, nunca cableado a admin) | editar un archivo abierto no dispara el diálogo de conflicto de mtime |
| 3.5 | **El verificador LLM pasa a política**: `ai.policy.verifier = off \| on_close \| always`; default `off` en `strong`, `on_close` en `bunker`. Se conserva el contrato de **veredicto tipado** (barato y útil) y se retira el pase adversarial + refutador obligatorios en `strong` | en `strong`, una consulta limpia baja de ~12 llamadas LLM a ≤5 |
| 3.6 | Sustituir la propiedad "no miente" por dos mecanismos baratos: (a) regla de prompt "toda afirmación sobre el código cita `path:línea`", (b) **suite de regresión de trampas** (§M6) en CI en lugar de impuesto en runtime | las 20 trampas `false_friend` siguen en 20/20 con el verificador apagado |

**Riesgo explícito:** 3.5 puede reintroducir alucinación. Mitigación = 3.6 + el
hecho de que con edición el oráculo es el compilador, no la retórica. Si la
suite de trampas cae por debajo de 20/20 en `strong`, se vuelve a `on_close`
antes de seguir.

### M4 — Contrato de proyecto, memoria y sesiones (≈1 semana)

> **M4.1 implementado y medido (2026-10-26).** Consulta *"¿con qué comando
> compilo este proyecto y desde dónde debo correr los tests?"* contra el 27B:
>
> | | Sin contrato | Con contrato |
> |---|---|---|
> | Llamadas LLM | 5+ (cortado por timeout de 560 s) | **2** |
> | Spawns | 4 (explore, 2×read, search) | **0** |
> | Resultado | veredicto `dudoso`, **sin respuesta** | respuesta correcta en **104 s** |
>
> Dos hallazgos del camino, los dos sobre el contrato de prompts y no sobre la
> feature:
>
> 1. **Inyectar el fichero no basta.** El system prompt del piloto prohíbe
>    responder sin un spawn ("nunca respondas solo de memoria del modelo"), y un
>    fichero que da el usuario encaja en esa prohibición: el piloto exploró,
>    leyó `CMakeLists.txt`, buscó `ctest`, se llevó un `dudoso` y solo entonces
>    decidió leer el `AGENTS.md` que ya tenía en el prompt. Hubo que declarar
>    explícitamente que el contrato **es evidencia**. Es la misma patología que
>    §1.1: la regla anti-alucinación hace rehacer trabajo ya hecho.
> 2. **El harness corría sin contrato mientras el producto lo tenía**, porque su
>    `opts.workspace_root` es el directorio de sesión y no el proyecto. Otra
>    divergencia harness/producción de la familia de §0bis; ahora se pasa
>    explícito.
>
> **M4.2 implementado.** `/init` inspecciona el árbol (sin ejecutar nada) y deja
> el borrador: sistema de build por marcador, comando de test, de run, lenguajes
> dominantes, directorios que no son fuente, e **indentación medida en las
> fuentes**. Lo que no puede saber queda como TODO en vez de inventado.
>
> Dos reglas que sigue: un script envoltorio en la raíz o una tarea de
> `.tuide/config.json` manda sobre el comando genérico del sistema de build (un
> `cmake --build build` pelado sería correcto y a la vez inútil aquí); y nunca
> pisa un `AGENTS.md` existente sin `--force`.
>
> Compara además la indentación medida con `.clang-format` y lo dice cuando no
> coinciden — un fichero de estilo desfasado es peor que ninguno, porque el
> agente lo obedece y rompe el estilo real. En este repo detecta justo eso, más
> `./compile.sh`, el patrón de binarios de test sueltos (sin `ctest` que
> invocar) y `.claude/` entre los árboles excluidos.
>
> **M4.3 implementado.** `admin_clear_session` era un `remove_all` y corría en
> cada arranque en frío, en `/new` y al abrir una consulta que no continuaba la
> anterior: el notebook de ayer desaparecía sin preguntar. Ahora la sesión se
> mueve a `.tuide/ai/sessions/<timestamp>/`, con `/sessions` para listarlas y
> `/resume [id]` para seguir una. **Reanudar copia, no mueve**, así que
> ramificar sale gratis: recuperar una investigación vieja no te cuesta la
> actual (se archiva antes). Una sesión sin consulta ni evidencia se descarta en
> vez de ensuciar el historial, y se poda a 20.
>
> **M4.5 implementado.** `.tuide/ai/memory.md` + `/remember`, `/memory`,
> `/forget <n>`, cargado en el prefijo inmutable junto al contrato. Lo escribe
> el usuario, no el agente: M4.1 mostró que lo que paga es el hecho escrito por
> quien conoce el repo, y una memoria autoescrita necesita un filtro de calidad
> que no existe.
>
> **Tercer hallazgo de la misma familia (y el más caro de los tres).** Inyectar
> tampoco bastó aquí, una capa más abajo: **el verificador solo ve el notebook
> tipado**, así que una afirmación apoyada en el contrato o en la memoria es
> indemostrable por construcción. Preguntado *"¿los tests de l2_wave que fallan
> son una regresión de mi trabajo?"* —con la respuesta en la memoria— el piloto
> se pasó 10 turnos persiguiendo stubs de git y murió en el tope de verificación
> con `dudoso`, dos veces. Con el verificador recibiendo ambos bloques: **2
> llamadas** y responde desde la memoria. Sigue prefiriendo `ask_user` a cerrar
> (bucket P11), lo cual para una atribución causal es defendible.
>
> Patrón a retener para M1/M3: **cada vez que se añade una fuente de verdad
> nueva, hay que declararla evidencia en los tres sitios** (prompt del piloto,
> prompt del verificador, y el harness que mide). Olvidar uno no da un error:
> da rondas de más.
>
> **M4.4 implementado — M4 completo.** `@fichero` se resuelve contra el árbol
> (basta un sufijo o el nombre; prefiere ruta exacta > sufijo > nombre >
> subcadena, y a igualdad la ruta más corta), adjunta un extracto y entra como
> evidencia en el prefijo inmutable, declarada en los dos prompts. Salta los
> mismos directorios que la búsqueda, así que **no resuelve a la copia de
> `.claude/worktrees`**. Una `@` pegada a texto no es mención (los correos
> sobreviven) y lo que no resuelve se reporta en vez de desaparecer.
>
> Solo ficheros: `@símbolo` tendría que adivinar entre fichero y símbolo del
> mismo nombre, y para símbolos ya está el explorador.
>
> Medido contra el host real: el recorrido cuesta **67 ms** una vez por consulta,
> y *"¿qué transportes soporta @llm_client.hpp?"* se respondió en **2 llamadas
> sin spawns**, citando el fichero y cruzándolo con la memoria durable para
> nombrar el endpoint realmente en uso. Las tres fuentes humanas (contrato,
> memoria, mención) operando juntas.
>
> **Estado de la suite tras M4:** 12 tests en rojo, los 11 preexistentes más
> `llama_host_comm_test`, que es una sonda de entorno y falla porque el servidor
> de embeddings del host (`:18765`) está caído; no enlaza ningún fichero de este
> trabajo.

| # | Trabajo | Aceptación |
|---|---|---|
| 4.1 | ✅ **HECHO** — `AGENTS.md` (raíz + global del usuario) en el prefijo inmutable: comandos de build/test, convenciones, zonas prohibidas | el agente compila con el comando del proyecto sin que se lo digas |
| 4.2 | ✅ **HECHO** — `/init` genera el `AGENTS.md` inicial desde el árbol (`src/ai/project_contract.*`) | proyecto nuevo → contrato útil en un comando |
| 4.3 | ✅ **HECHO** — sesiones archivadas en `.tuide/ai/sessions/`, `/sessions`, `/resume [id]`; el arranque y `/new` archivan en vez de borrar | reabrir la app y continuar la investigación de ayer |
| 4.4 | ✅ **HECHO** — `@fichero` en el input (`src/ai/ai_mentions.*`); `@símbolo` descartado a propósito | adjuntar contexto sin describirlo en prosa |
| 4.5 | ✅ **HECHO** — memoria durable `.tuide/ai/memory.md` + `/remember`, `/memory`, `/forget` | "usa siempre `./compile.sh`" persiste entre sesiones |

### M5 — Amputación del lastre (≈1 semana) · *hace baratos los demás hitos*

| # | Trabajo | Aceptación |
|---|---|---|
| 5.1 | Sacar del target `tuide` lo no alcanzable: `l2_effect_registry/slice/summary`, `level2_session`, `level1_agent`, `l2_explore_a*`, `l2_problem_frame`, `l2_entityness`, `l2_graph_query_profile`, `l2_pack_review`, `context_pack`, `coding_embed_*`, `embedding_backend`. Lo que la batería aún necesite → target aparte `tuide-legacy-harness` | binario sin ~40 K líneas muertas; tiempo de compilación y tamaño medidos antes/después |
| 5.2 | Portar el autofix (M3.2) **antes** de retirar `level2_autonomous_loop` | ninguna capacidad perdida en la amputación |
| 5.3 | `level0_router`: conservar solo el parser de slash commands; retirar el clasificador NL | `/build`, `/git`, `/search` siguen funcionando sin L0 completo |
| 5.4 | **Borrar las reimplementaciones Python** (`*_lite_local.py`, `probe_admin_*.py`): las baterías invocan el binario vía `l2_harness_cli agent-run` | una sola implementación de la lógica; fin de "portear el fix al otro lado" |

### M6 — Evaluación que cierra el lazo (≈1 semana)

> **Suite de trampas implementada (2026-10-26)** — `tools/l2_wave/trap_suite.py`.
> Es el prerequisito de la decisión "¿gate apagado por defecto en `strong`?":
> convierte una opinión en un dato.
>
> Lo que no existía y hubo que construir:
> - `admin_battery.py` corre siempre con `--script` (cerebro de guion): mide
>   despacho y legalidad, **no puede medir si el modelo miente**.
> - `probe_admin_pilot.py` sí usa modelo real, pero reimplementa el lazo en
>   Python con otra dieta (§0bis).
> - `admin-run` **no tenía interruptor** para el verificador, así que el A/B era
>   imposible. Ahora: `--verifier on|off`, `--refuter on|off`.
> - El binario emite `summary.json` con lo que `admin_session_metrics.
>   score_session` consume (gesto de cierre, veredictos tipados, juicios del
>   gate, terminal, llamadas por rol). Una sola implementación del criterio.
>
> El scorer ya traía el arreglo de negación (P10) hecho: `_phrase_negated_at`
> mira antes y después de la frase, con la excepción de "no obstante".
>
> ```bash
> # Tirada nocturna (10 h): decide primero lo que decide la pregunta.
> python3 tools/l2_wave/trap_suite.py --budget-hours 10
> # Reanudar una tirada cortada:
> python3 tools/l2_wave/trap_suite.py --resume --out .tuide/ai/evals/trap_suite_<id>
> ```
>
> Orden de la cola: 28 trampas × 2 brazos × rep 1 → rep 2 → muestra repartida de
> 24 casos normales (mide la **fricción** que quita el gate: `ask_user`,
> llamadas) → rep 3. Brazos intercalados por caso, así que un corte deja pares
> completos. Reanudable (`runs.jsonl`), informe reescrito tras cada run,
> cortocircuito si el backend se cae (5 fallos rápidos seguidos), y un run
> cortado por timeout se reconstruye desde el `state.json`/`turns/` que el
> binario persiste — un corte sigue diciendo "no cerró", que es justo la señal
> de fricción.
>
> Secuencial a propósito: medido en vivo, dos `admin-run` simultáneos se pelean
> por el único modelo del host y los tiempos dejan de ser comparables.
>
> Guarda `AGENTS.md` y `memory.md` en `context/` porque entran en cada prompt:
> sin eso el informe no es reproducible.

| Suite | Contenido | Objetivo de tiempo | Cuándo |
|---|---|---|---|
| **Rápida** | 20 casos de localización/respuesta, modelo barato, paralelo, fixtures deterministas | **< 10 min** | cada cambio |
| **Edición** (nueva, la que importa) | 15-25 tareas en repo fixture con **compile + test como oráculo**: crear archivo, editar 2 archivos, arreglar un build roto, añadir un test | < 40 min | cada cambio del camino de edición |
| **Honestidad** | las 20 trampas `false_friend` + 8 `wrong_layer` (`tools/l2_wave/trap_suite.py`) | nocturna hoy; < 15 min cuando haya modelo pequeño | cada cambio de política |
| **Nocturna** | los 100 casos, contra el binario de producción | sin límite | antes de cada release |

Salida en `.tuide/ai/evals/<fecha>/` (JSON + markdown), con `llm_calls`,
tokens, coste, veredicto y diff por caso.

### M7 — Diferenciación que nadie puede copiar (post-M3, continuo)

| # | Trabajo | Por qué es único |
|---|---|---|
| 7.1 | **Depurador en el lazo (DAP)**: tools `debug_run(target\|test)`, `breakpoint(file:line)`, `step`, `inspect(expr)`, `backtrace` | el agente lee **valores reales en ejecución** en vez de razonar sobre el código. Ningún harness CLI puede hacerlo |
| 7.2 | **Core dumps como evidencia**: `core_analyze(path)` → heap, símbolos, stack | forense post-mortem dirigido por el agente |
| 7.3 | **Quick-fixes de clangd** aplicados por el agente (`apply_code_action`) | arreglos deterministas a coste cero de generación |
| 7.4 | **Perfil búnker empaquetado**: un binario con `llama-server` + modelo + clangd + gdb, offline, con `admin_v1` + gate | la única opción creíble en entornos sin red; es tu nicho defendible |
| 7.5 | `trail`/`dataflow`/causal-flow como **features de IDE para humanos** de primera clase (ya empezado) | valor sin depender de que el modelo las elija |

---

## 4. Secuencia y calendario

```
S1      S2      S3      S4      S5      S6      S7      S8      S9
M0 ████████
M1         ████████████████
M2              ████████████
M3                       ████████████
M4                  ████████
M5    ████ (5.1/5.4 en paralelo desde el principio)
M6                            ████████
M7                                         ████████████ …
```

Orden de ataque recomendado: **M0 → (M5.1, M5.4 en paralelo) → M1 → M2 → M3 →
M4 → M6 → M7**. Total hasta paridad competitiva: **≈7-9 semanas**; hasta
diferenciación (M7.1) **≈11**.

Dos cortes de entrega visibles:
- **Hito "se siente moderno"** (fin de M0 + M1.1-1.3): streaming, sin errores de
  formato, sesión persistente, tokens visibles. Es el 80 % de la sensación de
  retraso con el 20 % del trabajo.
- **Hito "edita de verdad"** (fin de M2 + M3): crea archivos, edita
  multi-archivo, compila, se revierte, y tú revisas el diff.

---

## 5. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Apagar el verificador reintroduce alucinación | Suite de honestidad en CI (M6) como guardia; regla "cita `path:línea`"; `on_close` como vuelta atrás de un flag |
| Streaming rompe el idle/CPU del TUI | Coalescer por `UI_WAKE` + `UiActivityGate` (patrón ya usado para el stdout de compilación: `ai_controller.cpp:716`) |
| Calidad desigual del tool-calling entre proveedores | Caps por backend + fallback automático al contrato `admin_v1` JSON+GBNF; el perfil búnker ya es ese camino |
| La amputación rompe el harness CLI | M5.1 mueve, no borra: target `tuide-legacy-harness` hasta que M6 lo reemplace |
| Reescribir el loop mientras el producto funciona | `ai.agent_v2=true` detrás de flag; `admin_v1` intacto como perfil búnker hasta que la suite de edición supere al camino viejo |
| Multi-edit atómico corrompe el árbol | Snapshot sombra previo obligatorio + aplicación en temporal + `rename` + rollback (`edit_snapshot.hpp` ya existe) |

---

## 6. Métricas

| Métrica | Hoy | Objetivo tras M0-M3 |
|---|---|---|
| Llamadas LLM por consulta (perfil `strong`) | ≈12 (50+ peor caso) | **≤5** (≤15 peor caso) |
| Reintentos por formato | 28/100 casos | **0** |
| Fracción `ask_user` | 56 % | **≤20 %** |
| Tareas de edición completadas con build verde | no medible (sin gate, sin `write_file`) | **≥70 %** de la suite de edición |
| Trampas `false_friend` rechazadas | 20/20 | **20/20** (no negociable) |
| Latencia hasta el primer carácter visible | segundos, sin feedback | **streaming inmediato** |
| Tokens/coste por consulta | no medido | visible en panel + trace |
| Líneas de `src/ai/` en el binario | ~86 K | **≤45 K** |
| Ciclo de evaluación iterable | ~19 h | **< 10 min** (rápida) |

---

## 7. Lo que NO hay que hacer

1. **No convertir el gate epistémico en la identidad del producto.** Es una
   política del perfil búnker. Con modelo fuerte, el oráculo es el compilador.
2. **No invertir más en tools de exploración exóticas para el agente.** El
   modelo no las elige. Su sitio es la UI para humanos.
3. **No resucitar embeddings/RAG para el harness** (Fase F del master-spec). El
   cuello de botella es que el explorador ve 9 KB y el piloto no puede editar.
4. **No añadir MCP antes de M3.** Superficie sin arreglar lo que duele. Después
   de M2 es casi gratis (el tool belt ya estará normalizado).
5. **No tocar más constantes a mano** hasta que exista la telemetría de M0.5.
   El historial reciente demuestra que ese camino es ruido.
6. **No medir en SWE-bench antes de M2.2/M3.1.** Sin `write_file` ni gate de
   compilación, mediría la ausencia de las tools, no la calidad del agente.
7. **No mantener dos implementaciones** (C++ y Python). M5.4 no es opcional: es
   la condición para que cualquier medida futura signifique algo.

---

## 8. Resumen

El foso no es que el agente razone con más rigor que los demás: es que **vive
dentro de un IDE con compilador, LSP, tests y depurador**. El plan es dejar de
pagar por la retórica (gate obligatorio, tools que nadie elige, embeddings
muertos) y empezar a cobrar por el oráculo (compila, los tests pasan, clangd
calla, el depurador dice que vale 3) — con la fontanería y el tool belt de un
harness de 2026 por debajo, y el perfil búnker intacto como capacidad que
ninguno de ellos tiene.
