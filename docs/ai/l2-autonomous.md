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
    "auto_download": false,
    "clarify_pushback_max": 3
  }
}
```

Máquinas más justas: `"model_id": "qwen2.5-coder-1.5b-instruct-q4_k_m"`.
`clarify_pushback_max`: cuántas veces se rechaza un `done next=clarify` prematuro
(pidiendo más `get_code_of`/tools) antes de aceptar el clarify. `0` = sin pushback.

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
    "n_ctx_remote": 32768,
    "max_steps": 32
  }
}
```

Clave: `ai.level2.api_key` o env `TUIDE_L2_API_KEY` / `OPENAI_API_KEY`.
Sirve `llama-server`, vLLM, DeepSeek, OpenAI, etc. (`POST {api_base}/chat/completions`).
`n_ctx_remote` (default **32768**) escala pack/prompt ambicioso; no se auto-detecta del proveedor.

### Híbrido: tuide en Linux ARM + llama-server Metal en el Mac

El IDE (gdb, inotify, PTY, L1 CPU) corre en una VM Linux. El GGUF grande y los
embeddings van en el Mac con Metal. TIDE **no** descarga ni arranca `llama-server`
si el host de embeddings no es loopback: solo hace attach HTTP.

En el **Mac**:

```bash
# GGUF en ~/.cache/tuide/models/l2/ (y l1/) y embed/intent/
# llama-server con Metal (PATH, TUIDE_LLAMA_SERVER, o cache runtime/llama-b10333)
./tools/run_host_llama.sh                 # hub HTML: Lanzamiento | Inspección
./tools/run_host_llama.sh --llm 7b        # hub; preselecciona chat 7B
./tools/run_host_llama.sh --llm 14b --no-embed
./tools/run_host_llama.sh --foreground    # hub en esta terminal
./tools/run_host_llama.sh --stop          # para hub/spy/llama-server en los puertos tuide
./tools/run_host_llama.sh -y              # autoelige GGUF y abre Inspección
./tools/run_host_llama.sh --ui gui        # listas nativas (legado)
./tools/run_host_llama.sh --ui text       # menú TTY
```

El hub (`http://127.0.0.1:18767`) tiene dos modos:

- **Lanzamiento** — catálogo (Qwen L1/L2 + nomic y GGUF ya en disco), descargar, importar URL de Hugging Face o ruta local, arrancar / parar / reiniciar chat y embed por separado.
- **Inspección** — el visor spy (historial VM + prompts locales). No gestiona procesos.

En Mac el hub corre en Terminal.app (`open -a Terminal`). El proxy imprime **tokens en vivo** en esa ventana (la VM sigue por HTTP). `--no-spy` desactiva el proxy. `--no-web` vuelve al picker legado.
Puertos: UI `:18767` (loopback), chat `:8080`, embeddings `:18765` (bind `0.0.0.0` para la VM).

`llama-server` de chat se configura en la pestaña **Lanzamiento** (flash-attn, KV `q8_0`,
un slot, hilos P, embeddings en CPU, draft 1.5B, pensamiento en vivo en Qwen3/R1).
Los valores se aplican al Lanzar o Reiniciar. Overrides por env: `TUIDE_HOST_FLASH_ATTN`,
`TUIDE_HOST_CACHE_TYPE`, `TUIDE_HOST_THREADS`, `TUIDE_HOST_EMBED_NGL`, `TUIDE_HOST_DRAFT`,
`TUIDE_HOST_DRAFT_GGUF`, `TUIDE_HOST_THINKING`.

En la **VM** (Settings F10 o `.tuide/config.json`):

```json
"ai": {
  "level2": {
    "mode": "remote",
    "api_base": "http://192.168.64.1:8080/v1",
    "api_model": "qwen2.5-coder-32b-instruct-q4_k_m",
    "n_ctx_remote": 32768
  },
  "level0": {
    "embeddings": {
      "server_host": "192.168.64.1",
      "server_port": 18765
    }
  }
}
```

Host típico desde la VM: UTM `192.168.64.1`, OrbStack `host.orb.internal`.
Overrides sin reescribir config: `TUIDE_L2_API_BASE`, `TUIDE_L2_API_MODEL`,
`TUIDE_EMBED_HOST`, `TUIDE_EMBED_PORT`.

## Uso en la app

1. `ai.level2.mode = local|remote` (Settings) **o** override en el tab AI
2. Prompt en tab AI (L1 escala / siembra sesión) **o** `/l2_session bootstrap <query>` + `/l2_run`
3. El tab AI streamea líneas `L2 ▸ fase=explore|edit|compile …`
4. `/cancel` aborta el loop

Harness (`mode=harness`) sigue disponible para depurar a mano con `/l2_tool` / `request.json`.

### Toggle Local / Remoto (híbrido)

En el footer del tab AI (junto a Workflow) hay un selector **Local | Remoto**. Es un
**override de sesión**: no reescribe el default de Settings. También: `/backend local|remote`.

Flujo típico de handoff:

1. Workflow **Plan** + backend **Remoto** → investigación con pack/prompt amplios →
   `answer.md` + sesión continuable.
2. Cambiar a **Local** + workflow **Agent** → Enter / follow-up → albañilería sobre el
   mismo disco (`.tuide/ai/l2/`). El prompt se **re-corta** al budget local; no se
   reenvía el prompt gigante del remoto.

Tools / FS / compile siguen **siempre en local**; solo el `propose` del brain cambia.

### Presupuesto de contexto (`L2ContextBudget`)

Un solo scaler (`src/ai/l2_context_budget.*`) deriva caps de prompt/pack/obs/resume del
`n_ctx` efectivo:

| Backend | n_ctx efectivo |
|---------|----------------|
| local | `min(ai.level2.n_ctx, techo por MemAvailable)`; floor 4096 |
| remote | `ai.level2.n_ctx_remote` (default 32768) |

Baseline a `n_ctx=8192`: explore ~10k / edit ~8k / pack ~9k. Escala lineal y clampea
(explore máx. 48k). En arranque local, si `pack.md` supera ~1.2× el budget, se compacta
en disco (`L2 ▸ pack recortado…`).

Banner: `L2 ▸ arranque autónomo (remote n_ctx=32768 pack≈36000) …`.

## Checklist equipo de prueba

1. Instalar `ai-runtime` + `ai-l2` (o levantar API remote).
2. Poner `mode=local` o `remote` en config (o usar el chip del chat).
3. Reiniciar tuide / refresh settings.
4. Prompt: *añade un tab prueba con texto fijo…*
5. Verificar streaming de fases y compile; `git diff` sin anomalías.
6. `/model` debe listar estado L2 (incluye override y `n_ctx_remote` si aplica).

## Contexto / compile feedback

- Prompt L2 cabe en el budget del backend activo (local ≈ `n_ctx`; remote ≈ `n_ctx_remote`).
  No se manda `session.md` entero; el disco puede ser más rico que el slice del prompt.
- **Tool guide solo en system prompt** (no se duplica en `session.md`).
- **Flujo plan → pack:** con **Phase A/B** (flag `L2_EXPLORE_PHASE_A` **on** por
  defecto): `explore_a` juzga peeks (`a_judge`/`a_done` → `loci[]`, sin `pack.md`);
  `explore_b` materializa pack desde loci. Detalle:
  [`docs/plans/l2-explore-phase-a-b.md`](../plans/l2-explore-phase-a-b.md). Hot path A/B
  **sin LSP** ([degradación](l2-explore-no-lsp.md)). Override: `L2_FEAT_L2_EXPLORE_PHASE_A=0`
  restaura explore mezclado: preferir `plan` en el **primer** paso
  (`{"action":"plan","targets":["path:Symbol","path:A-B",…]}`; máx. 16; evitar path bare;
  4–8 targets anclados). Máx. ~8 tools sueltos antes del primer plan (soft `_nudge:_`).
  Tras pack cubierto: extras con `tools` batch (máx. 4); si siguen las tools, otro
  `_nudge:_` pide `done next=edit`. No repetir path con ventanas solapadas.
  El runtime **merge** watchlists (plan2 no pisa plan1; bootstrap resetea `pack.md` /
  watchlist — no reinyecta targets de una sesión previa), normaliza bare→símbolo/ventana por
  outline + **search-in-file** (prioriza search; rechaza símbolos basura tipo `const`),
  prioriza por **roles** (decl/id_const/layout/control/api_fn)
  con slot mínimo por rol (layout×2 temprano: decl boxes + click-targets), tope ~25%/pieza,
  **auto-refetch** anclado a `path:line`, y escribe `.tuide/ai/l2/pack.md` (~9k chars).
  Overflow del pack **no** hace head+tail del documento entero (borra el medio): recorta
  outlines primero; conserva **Headers** (decl / `#include`) con Fragments. Si el plan
  apunta a un `.cpp`, el runtime añade el **header hermano** (mismo stem `.hpp`/`.h`)
  como fragmento `path:1` (preámbulo de declaraciones). Bare sin hit fuerte se **omite**.
  Diversidad también por **archivo** (1 fragmento/path). `path:line` explícito se preserva.
  Ruido merge débil se aplaza. **`pack_incomplete`** = cero fragmentos, o ningún ident de
  `query:`/`instruction:` (snake / `path:Sym` / CamelCase largo) aparece en el pack. Sin
  facetas hardcoded (tab/shortcut/i18n). Pushback lista los idents que faltan. Truncado ≠
  bloqueo si hay hit.
- **map_stale:** si la `query:` del `map_last` solapa poco la Instruction, bootstrap marca
  `map_stale`, **no inyecta** el mapa rankeado en `session.md` (stub + aviso) y el prompt
  pide `search`/`plan` anclado. `map_initial.md` guarda el mapa completo por si hace falta;
  tras compile OK **no** se restaura en sesión si era stale.
- **Needles del pack:** solo líneas `query:` / `instruction:` / `seeds:` más args de
  tools en Observations (no el párrafo de guía). `wrong_symbol` no dispara si el nombre
  coincide con el `path:Symbol` pedido o con `path:line`.
- **Truncado de cuerpos:** `get_code_of` por defecto usa **head+tail** si el símbolo supera
  `max_lines` (~120). Con `path:line` dentro de un símbolo grande → **ventana** alrededor
  de la línea (refetch = ventana adyacente). Marca `[TRUNCATED]` con `symbol_span` /
  `missing_lines` / `refetch`. Outlines en pack se filtran por needles.
- Bootstrap guarda `.tuide/ai/l2/map_initial.md` (mapa completo).
- Explore slim del mapa en el prompt (antes del pack): detalle solo top‑5; resto nombres.
- Observations en explore: cola ~3.5k chars en el prompt; en disco, tras pack, se recortan
  a ~8k (`kMaxObservationCharsPacked`). Cada tool post-pack se recorta por turno
  (~40 líneas / ~2.4k chars); si un solo turn sigue inflando la sección, se trunca el
  cuerpo. Compactación también en `phase=edit`.
- Si un cuerpo viene `[truncated]`, pedir `get_code_of path:Metodo` (no inventar código).
- Tras `edit` OK el runtime compila. **Compile OK no cierra**: restaura el **mapa inicial**
  en la sesión, marca `map_review` y pregunta **«¿algo más?»** (`plan` / `edit` /
  `done` sin `next`).
- Tras compile fail, Observations guarda **cola** del stderr (`kMaxCompileLogLines`, ~40) + old/new de hunks (no el log completo). Si el log cita símbolos *undeclared*, el feedback pide declaración en el `.hpp` hermano.
- Tras **edit apply fail** (search no encontrado/ambiguo), Observations guarda `edit_feedback` con error + search/replace + pista de declaración hermana; el tab muestra el error. Así el modelo no reemite el mismo hunk a ciegas.
- Si el modelo se queda sin contexto, el error cita `ai.level2.n_ctx` (no L1).
- **Compactación del mapa (disco):** tras tools/plan en explore, el
  `## Ranked map` queda en líneas de nombre salvo stems/paths ya tocados; se descarta `## Bodies`.
  Tras compile OK se reinyecta el mapa inicial completo (salvo `map_stale`).

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
4. Si el cierre fue `done`/`clarify`, la sesión queda **continuable** (`continuable=true` en
   `state.json`). El siguiente Enter en el tab AI **no** hace bootstrap: reabre L2 con
   Instruction + `## Follow-ups` + `answer.md` / pack previos (`resume=1`). El modelo elige
   atajo (`edit`/`synthesize`) o explore/plan. Cambiar Ask→Agent **no** resetea el contexto.
5. **Reset** (botón en el footer del tab AI, o `/new`/`/reset`) borra `.tuide/ai/l2/` y limpia
   el transcript: el próximo mensaje arranca L0→L1→bootstrap de cero.

## Handoff code_edit (modificar código)

1. L1 detecta pedido de cambio → elabora **mapa rankeado completo** (catálogo ancho, sin bodies).
2. Escribe `.tuide/ai/map_last.md` y siembra `session.md` con ese mapa como `## Ranked map`
   (y copia en `l2/map_initial.md`).
3. L2 en **explore** elige targets con `action=plan`; el runtime arma el **code pack** y
   decide edit; tras compile OK revisa el mapa inicial («¿algo más?»).

## Tests sin modelo

```bash
./build/level2_autonomous_loop_test
./build/level2_session_test
./build/level2_debrief_test
./build/l2_context_budget_test
./build/l2_action_test
./build/search_replace_test
```
