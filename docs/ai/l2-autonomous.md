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

- Tras compile fail, Observations guarda **cola** del stderr (`kMaxCompileLogLines`, ~40) + old/new de hunks (no el log completo).
- En `phase=edit` el prompt al brain usa cola de sesión (~12k chars), no el `session.md` entero.
- Si el modelo se queda sin contexto, el error cita `ai.level2.n_ctx` (no L1).

## Handoff code_edit (modificar código)

1. L1 detecta pedido de cambio → elabora **mapa rankeado completo** (catálogo ancho, sin bodies).
2. Escribe `.tuide/ai/map_last.md` y siembra `session.md` con ese mapa como `## Ranked map`.
3. L2 en **explore** usa ese mapa como punto de partida (prioriza score alto), lee cuerpos con tools y decide el edit.

## Tests sin modelo

```bash
./build/level2_autonomous_loop_test
./build/level2_session_test
./build/search_replace_test
```
