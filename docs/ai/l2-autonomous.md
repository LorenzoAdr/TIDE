# L2 autonomous (Fase E) — local / remote

El orquestador (`Level2Session`) + loop (`run_level2_autonomous`) están cableados.
En esta máquina de desarrollo puede no haber RAM/GPU para el GGUF; el cableado se prueba
con el test scripted y en el **equipo preparado** con modelo real.

## Config (`.tuide/config.json`)

### Local (llama-cli + GGUF)

```json
"ai": {
  "level2": {
    "mode": "local",
    "model_id": "qwen2.5-coder-7b-instruct-q4_k_m",
    "max_steps": 32,
    "max_tokens": 2048,
    "n_ctx": 8192,
    "temperature": 0.1,
    "auto_download": false
  }
}
```

Máquinas más justas: `"model_id": "qwen2.5-coder-1.5b-instruct-q4_k_m"`.

Descarga:
- Toolpacks → **AI coder model (L2)** (`ai-l2`)
- o tab AI: `/model download_l2`

GGUF en `$XDG_CACHE_HOME/tuide/models/l2/` (o `ai.models.cache_dir`).

### Remote (OpenAI-compatible)

```json
"ai": {
  "level2": {
    "mode": "remote",
    "api_base": "http://127.0.0.1:8080/v1",
    "api_model": "qwen2.5-coder-7b-instruct",
    "api_key": "",
    "max_steps": 32
  }
}
```

Clave: `ai.level2.api_key` o env `TUIDE_L2_API_KEY` / `OPENAI_API_KEY`.
Sirve `llama-server`, vLLM, DeepSeek, OpenAI, etc. (`POST {api_base}/chat/completions`).

## Uso en la app

1. `ai.level2.mode = local|remote`
2. Prompt en tab AI (L1 escala / siembra sesión) **o** `/l2_session bootstrap <query>` + `/l2_run`
3. El tab AI streamea líneas `L2 ▸ fase=explore|edit|compile …`
4. `/cancel` aborta el loop

Harness (`mode=harness`) sigue disponible para depurar a mano con `/l2_tool` / `request.json`.

## Checklist equipo de prueba

1. Instalar `ai-runtime` + `ai-l2` (o levantar API remote).
2. Poner `mode=local` o `remote` en config.
3. Reiniciar tuide / refresh settings.
4. Prompt: *añade un tab prueba con texto fijo…*
5. Verificar streaming de fases y compile; `git diff` sin anomalías.
6. `/model` debe listar estado L2.

## Contexto / compile feedback

- Prompt L2 cabe en `ai.level2.n_ctx` (p. ej. 8192): explore ~**10k** chars de sesión + system;
  edit ~**8k**. No se manda `session.md` entero.
- **Tool guide solo en system prompt** (no se duplica en `session.md`).
- Explore slim del mapa en el prompt: detalle/snippet solo en el **top‑5**; el resto es
  línea de nombre. Tras tools, compactación en disco (detalle solo en stems tocados).
- Observations en explore: cola ~3.5k chars.
- **Batch de tools:** `{"action":"tools","calls":[…]}` (máx. 4) en un solo propose.
  Si un cuerpo viene `[truncated]`, el runtime indica pedir `get_code_of path:Metodo`
  del recorte concreto (no inventar código).
- Tras `edit` OK el runtime compila. **Compile OK no cierra** la sesión: vuelve a
  `phase=edit` con observation `compile_ok`; el modelo debe emitir más `edit` si faltan
  archivos, o `{"action":"done","summary":"…"}` (sin `next`) cuando la Instruction esté cubierta.
- Tras compile fail, Observations guarda **cola** del stderr (`kMaxCompileLogLines`, ~40) + old/new de hunks (no el log completo).
- Tras **edit apply fail** (search no encontrado/ambiguo), Observations guarda `edit_feedback` con error + search/replace; el tab muestra el error. Así el modelo no reemite el mismo hunk a ciegas.
- Si el modelo se queda sin contexto, el error cita `ai.level2.n_ctx` (no L1).
- **Compactación del mapa (disco):** tras cada tool en explore (y al pasar a `edit`), el
  `## Ranked map` queda en líneas de nombre salvo stems/paths ya tocados; se descarta `## Bodies`.

## Timing en `trace.ndjson`

Eventos con `duration_ms` (también `propose_ms` / `action_ms` / `total_ms`):

| event | qué mide |
|-------|----------|
| `l2_run_begin` / `l2_run_end` | loop autónomo completo (`total_ms`) |
| `l2_propose` | latencia del brain (modelo) |
| `l2_action` | tool / done / edit+compile runtime |
| `l2_step` | propose + action del paso |
| `l2_tool` / `l2_edit` / `l2_compile` | cada fase de sesión |
| `l1_complete` / `l1_needles_complete` | respuesta L1 |

`file_outline` espera al parse Tree-sitter (async) antes de responder; ya no devuelve `symbols=0` en frío.

## Debrief post-run (hechos + L1 opcional)

Al terminar el loop autónomo (`done` / `clarify` / error / cancel):

1. El runtime arma **hechos deterministas** desde `state.json`, Observations (`session.md`) y `l2/trace.ndjson`
   (outline `symbols=N`, `edit_fail`, compile ok/fail + ms, clarify, counts).
2. Se muestran siempre en el tab AI (`### L2 resumen (hechos)`) y se escriben en
   `.tuide/ai/l2/debrief.md`.
3. Si el backend L1 ya está `ready`, opcionalmente **redacta** esos hechos en español
   (`L2 ▸ debrief (L1, solo redacta hechos)`). No inventa causas: el prompt prohíbe
   reinterpretar `symbols=0` / «archivo OK» como «archivo no existe».

## Handoff code_edit (modificar código)

1. L1 detecta pedido de cambio → elabora **mapa rankeado completo** (catálogo ancho, sin bodies).
2. Escribe `.tuide/ai/map_last.md` y siembra `session.md` con ese mapa como `## Ranked map`.
3. L2 en **explore** usa ese mapa como punto de partida (prioriza score alto), lee cuerpos con tools y decide el edit.

## Tests sin modelo

```bash
./build/level2_autonomous_loop_test
./build/level2_session_test
./build/level2_debrief_test
./build/l2_action_test
./build/search_replace_test
```
