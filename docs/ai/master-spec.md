# Master Spec: Arquitectura de IA en 2 Niveles + Sistema de Atribución (tuide)

> **Estado:** plan de diseño (sin implementación).  
> **Origen:** propuesta generada con Gemini, contrastada contra el código real de tuide (`main`).  
> **Rama:** `feature/IA`

---

## 0. Veredicto ejecutivo

La propuesta de Gemini describe, en gran parte, **un IDE TUI que tuide ya es**. Tree-sitter, ripgrep, multi-LSP, FTXUI con `Event::Custom`, índice de workspace, Git, terminal PTY y buffers con undo **ya existen**. Lo que **no** existe es la capa LLM/agent (modelos, tool-calling, RAG vectorial, atribución AI/humano, autofix por LLM).

**Conclusión:** no construir un “editor TUI nativo + motor logístico” desde cero. Construir una **capa de IA** encima de la infraestructura actual, reutilizando `ISymbolProvider`, Tree-sitter, ripgrep, `EditorBuffer`, `UI_WAKE` / `UiActivityGate`, y el patrón de bundles/XDG cache.

Gemini acierta en la dirección (offline/búnker, C++ sin Python, 2 niveles, atribución en memoria). Falla al asumir greenfield y al mezclar “controlador logístico” con “RAG + embeddings + LLM” como si fueran el mismo problema.

---

## 1. Matriz de contraste: propuesta Gemini vs realidad

| Bloque Gemini | ¿Ya existe en tuide? | Evidencia | Acción recomendada |
|---|---|---|---|
| Editor TUI C++ / FTXUI | **Sí** | `src/ui/`, FTXUI v6.1.9 | No reimplementar |
| Tree-sitter chunking / parse | **Sí** (más que chunking) | `src/parser/tree_sitter_*`, grammars multi-idioma | Reutilizar AST/símbolos; no “añadir Tree-sitter” |
| ripgrep búsqueda léxica | **Sí** | `cmake/BundleRg.cmake`, `src/search/`, `src/indexer/workspace_indexer_rg.*` | Reutilizar como fallback y tool |
| clangd / LSP tipos y símbolos | **Sí** (multi-LSP) | `src/lsp/` (~4.8k LOC), `src/symbols/lsp_symbol_provider.*` | Exponer como tools del agent; no rehacer cliente |
| basedpyright / rust-analyzer / … | **Sí** | `language_server_spec`, bundles CMake | Gemini dice “pyright”; el producto usa **basedpyright** |
| `Event::Custom` / hilos desacoplados / CPU ~0 idle | **Sí** | `UiEventDispatcher`, `UI_WAKE`, `UiActivityGate` | Obligatorio reutilizar; no inventar otro wake loop |
| Indexador de archivos/símbolos | **Sí** (no vectorial) | `WorkspaceIndexer`, `SymbolWorkspaceIndexer` | Base del cold-start; no sustituir |
| Buffers + undo/redo | **Sí** | `EditorBuffer`, `TextRope`/`EditorText`, `undo_stack` | Extender para atribución; no dual-buffer paralelo |
| Git UI | **Sí** | `src/git/`, `git_panel` | No usar `git blame` en bucle; atribución en RAM |
| Terminal PTY | **Sí** | `src/terminal/` | Útil para builds/tests del agent |
| Config `.tuide.json` en raíz | **No** (path incorrecto) | Real: **`.tuide/config.json`** | Extender config existente |
| Task runner de build + stderr → Problems | **Parcial / No** | `src/build/` = entornos + `compile_commands` para clangd | Hay que diseñar build-as-tool; no confundir con lo actual |
| llama.cpp / GGUF / embeddings / vector DB | **No** | — | Nuevo |
| Tool-calling / agent loop / autofix LLM | **No** | Solo LSP code actions / quick-fix | Nuevo |
| Atribución AI vs humano por línea | **No** | Solo `dirty` + undo snapshots | Nuevo (valioso) |
| Auto-download modelos | **Patrón similar sí** | Bundles → `$XDG_CACHE_HOME/tuide/bundled/` | Reutilizar patrón XDG, no inventar otro layout |

### Lectura crítica de las cifras de Gemini

| Afirmación Gemini | Contraste |
|---|---|
| IDE “~350 MB RAM pluma” | README actual: **20–30 MB**. Los ~350 MB vienen del modelo Nivel 1, no del IDE. Mezclar ambos engaña. |
| Latencias &lt;100 ms en “80% tareas” | Ya las cumplen LSP/rg/TS **sin** LLM. Meter un 0.5B en el camino crítico empeora latencia para lo que ya es instantáneo. |
| Cold-start con rg + Tree-sitter | Correcto en espíritu — y **ya es el camino caliente** del IDE hoy. |

---

## 2. Desacuerdos y preguntas abiertas (responder antes de implementar)

Estas son las divergencias donde **no** conviene seguir a Gemini a ciegas. Hace falta decisión explícita.

### Q1 — ¿El Nivel 1 debe ser un LLM, o un orquestador determinista?

Gemini pone Qwen2.5-0.5B como “controlador del IDE”. Un 0.5B Q4 es frágil para tool-calling fiable (JSON schemas, multi-step, no inventar paths).

**Propuesta alternativa (recomendada):**

- **Nivel 0 / Router determinista (C++):** intents obvios → tools directas (abrir archivo, rg, hover LSP, git status, list dir) **sin** LLM.
- **Nivel 1 (LLM pequeño, opcional):** solo cuando el router no resuelve (lenguaje natural ambiguo, plan corto).
- **Nivel 2 (modelo mayor, opcional/offline o remote):** generación/refactor/autofix.

¿Aceptas este Nivel 0, o insistes en que *todo* pase por el 0.5B?

### Q2 — ¿RAG vectorial en la Fase 1 o más tarde?

Embeddings (`all-MiniLM-L6-v2.gguf`) + `sqlite-vec`/`USearch` añaden:

- ~90 MB modelo + índice en disco
- indexing pipeline (chunking, invalidación, watchers)
- memoria/CPU en background

Para navegación (“¿dónde se gestionan las conexiones?”) hoy ya hay: `workspace_symbols`, outline TS, rg, include graph, file picker fuzzy.

**Recomendación:** Fase 1 **sin** vector DB. Medir si LSP+rg+TS bastan. RAG = Fase 1.5 solo si el recall conceptual falla en repos reales.

¿Priorizas embeddings desde el día 1, o lo aparcamos?

### Q3 — ¿Modelo Nivel 1: 0.5B local obligatorio?

Alternativas a valorar:

| Opción | Pros | Contras |
|---|---|---|
| Qwen2.5-0.5B local (Gemini) | Offline, pequeño | Tool-calling débil |
| Qwen2.5-1.5B / 3B Q4 | Mejor agentes | Más RAM (~1–2 GB) |
| Solo remote (OpenAI-compatible / Ollama externo) | Sin embeber llama.cpp | No es “búnker” puro |
| Híbrido: local pequeño + endpoint opcional | Flexible | Más superficie de config |

¿El requisito “búnker/offline” es **hard** (sin red nunca) o **default offline con escape hatch**?

### Q4 — Autofix loop: ¿quién compila?

No hay task runner IDE. `src/build/` genera `compile_commands` para clangd; el usuario compila en el **terminal PTY**.

Opciones:

1. **Nuevo `BuildTaskService`:** lee comando de `.tuide/config.json`, subprocess async, parsea stderr → diagnostics artificiales.
2. **Reusar terminal:** el agent escribe el comando en el PTY y scrapea output (frágil).
3. **Solo LSP diagnostics** (sin compile): autofix sobre `publishDiagnostics` + code actions; más barato, menos “compile-fix”.

¿Cuál es el contrato de “compilación” para el loop? ¿Máximo 3 reintentos te sigue pareciendo bien?

### Q5 — Búfer dual vs journal sobre el buffer actual

Gemini propone Base Snapshot + Active Buffer por archivo. Ya tenemos:

- texto vivo en `EditorBuffer`
- undo vía snapshots COW (`EditorSnapshot`)
- flag `dirty`
- sync LSP incremental

Un segundo buffer paralelo **duplica** estado y choca con rope/LSP/semantic tokens.

**Recomendación:** un solo `EditorBuffer` + **Edit Journal** (autor, rango, timestamp, `edit_id`) + snapshot de “último save en disco” solo para diff de atribución (no un segundo editor).

¿De acuerdo, o hay un motivo fuerte para dual-buffer literal?

### Q6 — Alcance del Nivel 2 y del resto de la Fase 2

El mensaje de Gemini **se corta** en §2.2 (Edit Journal). Falta:

- modelo/tamaño del Nivel 2
- si Nivel 2 es local (GGUF) o API
- UI de chat / inline diff / apply hunks
- persistencia de atribución (¿solo RAM? ¿sidecar? ¿export?)
- gutter / coloreado AI vs humano
- interacción con Git (commits, exclude AI lines, etc.)

¿Puedes pegar el resto del plan (Fase 2 completa + Fase 3/Nivel 2)? Sin eso el Master Spec queda incompleto a propósito en §6.

### Q7 — Empaquetado: ¿llama.cpp estático en el binario?

tuide ya embebe herramientas como **blobs zstd** extraídos a cache XDG. Enlazar `llama.cpp` estático:

- sube mucho el binario y el tiempo de build
- complica el portable/glibc story (`tools/build-portable.sh`)

Alternativa alineada al proyecto: **backend LLM como proceso opcional** (como clangd), o librería dinámica/fetch, con feature CMake `TUIDE_BUNDLE_LLAMA=OFF` por defecto.

¿Preferencia: link estático siempre, bundle opcional, o proceso externo tipo Ollama?

---

## 3. Principios de diseño (refinados)

1. **Reutilizar, no reescribir.** LSP, TS, rg, Git, terminal, wake UI son APIs internas del agent.
2. **LLM fuera del hot path.** Navegación, símbolos, search y diagnósticos siguen siendo C++/LSP.
3. **Un hilo UI.** Workers de IA solo encolan resultados; wake con `UI_WAKE` / `Event::Custom`; respetar `UiActivityGate` en idle.
4. **Config en `.tuide/config.json`** (+ defaults globales XDG si aplica). Nada de `.tuide.json` nuevo en la raíz.
5. **Cache en `$XDG_CACHE_HOME/tuide/`** (ya usado: `bundled/`, `captures/`). Modelos → `…/tuide/models/`.
6. **RAM honestidad.** Publicitar footprint del IDE **sin** modelo; declarar por separado el coste del Nivel 1/2 cuando estén cargados.
7. **Seguridad de edición.** Toda mutación de buffer por IA pasa por el mismo camino que LSP edits (apply + undo group + journal de autor `AI`).
8. **i18n.** Cualquier UI nueva vía `i18n::tr`.
9. **C++17, sin Python en runtime** (coherente con el repo). Scripts de build/bundle pueden seguir siendo shell.

---

## 4. Arquitectura objetivo (capa nueva sobre lo existente)

```
┌──────────────────────────────────────────────────────────────┐
│ UI thread (FTXUI) — sin cambios de contrato                  │
│  Chat/AI panel · gutter atribución · progreso download       │
│  UI_WAKE ←── AiEventQueue                                    │
└───────────────────────────┬──────────────────────────────────┘
                            │
┌───────────────────────────▼──────────────────────────────────┐
│ src/ai/  (NUEVO)                                             │
│  AiRouter (Nivel 0, determinista)                            │
│  AiAgentLoop (tool-calling, límites, cancel)                 │
│  ToolRegistry → adaptadores a APIs existentes                │
│  ModelRuntime (llama.cpp u otro backend)                     │
│  ModelStore (auto-download + progreso)                       │
│  EditAttribution / EditJournal                               │
│  (opcional) EmbeddingIndex / VectorStore                     │
└───────┬───────────┬────────────┬─────────────┬───────────────┘
        │           │            │             │
   ISymbolProvider  ripgrep   EditorBuffer   BuildTask?
   TreeSitterSvc    search    WorkspaceModel Terminal/Git
```

### Módulos nuevos sugeridos

| Módulo | Responsabilidad |
|---|---|
| `src/ai/tool_registry.*` | Declara tools (`json` schema) y dispatch C++ |
| `src/ai/tools_fs.*` | list/read vía workspace; create/rename/delete con confirmación |
| `src/ai/tools_lsp.*` | hover, definition, refs, symbols, diagnostics |
| `src/ai/tools_search.*` | wrap `WorkspaceSearch` / rg |
| `src/ai/tools_git.*` | wrap `GitService` (read-only primero) |
| `src/ai/tools_build.*` | compile task (si Q4 = opción 1) |
| `src/ai/agent_loop.*` | turnos, tool calls, max steps, cancel |
| `src/ai/router.*` | Nivel 0 sin LLM |
| `src/ai/model_runtime.*` | abstracción backend (llama.cpp / remote) |
| `src/ai/model_store.*` | download async + UI progress |
| `src/ai/attribution.*` | journal + ranges por autor |
| `src/ai/rag/*` | **aplazado** hasta Q2 |

**No crear** `src/ai` que reimplemente parser, LSP client o search.

---

## 5. Plan por fases (refinado)

### Fase 0 — Cimientos (sin modelo todavía)

Objetivo: contrato interno usable por tests, sin GGUF.

1. `ToolRegistry` + tools de solo lectura: `list_directory`, `read_file` (buffer o disco), `workspace_search`, `document_symbols`, `hover`, `diagnostics`.
2. `AiRouter` con reglas/heurísticas mínimas (comandos slash o intents triviales).
3. Panel AI stub (FTXUI) que invoca tools y muestra resultados — **sin** LLM.
4. Cableado `UI_WAKE` + worker thread de demo.
5. Extender `.tuide/config.json` con bloque `"ai": { "enabled": false, ... }`.

**Criterio de salida:** desde el panel se puede preguntar “símbolos de este archivo” / “buscar X” y obtener datos reales del IDE.

### Fase 1 — Atribución + apply seguro (Gemini Fase 2, priorizada antes del LLM pesado)

Tiene más valor de producto y menos riesgo que embeber llama.cpp pronto.

1. **Edit Journal** en memoria por path abierto:
   - cada mutación registra `{author: User|AI|Lsp|System, start, end, t, op_id}`
   - coalescer tecleo de usuario (como undo groups)
   - AI apply = un `op_id` atómico + `push_undo` previo
2. Snapshot “last saved disk bytes/hash” en `WorkspaceModel` (ya hay load/save/conflict) para diff user/AI vs disco.
3. Gutter opcional / highlight de rangos AI (respetar `UiActivityGate`).
4. Rollback de una operación AI = undo hasta el snapshot marcado (reutilizar `undo_stack`, no Git).

**No** dual-buffer paralelo. **No** `git diff` en polling.

**Criterio de salida:** tras un apply AI simulado (fixture), la UI distingue líneas AI y Undo revierte con journal coherente.

### Fase 2 — ModelRuntime local + auto-download (Gemini 1.2)

1. Abstracción `ILlmBackend` (generate + chat + optional tools).
2. Implementación `LlamaCppBackend` detrás de `TUIDE_ENABLE_LLAMA` (OFF default hasta estabilizar).
3. `ModelStore`: paths bajo `$XDG_CACHE_HOME/tuide/models/`, download async, progreso vía eventos UI (mismo patrón que extracción de bundles).
4. Modelo inicial: **candidato** Qwen2.5-0.5B-Instruct Q4 — **provisional** hasta cerrar Q3; validar tool-calling real antes de fijarlo en piedra.
5. Nivel 1 solo para: clasificar intent, elegir tools, resumir diagnostics — **no** para sustituir rg/LSP.

**Criterio de salida:** con flag ON, descarga una vez, responde a un chat simple offline, CPU idle del IDE intacta con modelo descargado pero sin inferencia.

### Fase 3 — Agent loop + autofix acotado (Gemini 1.3–1.4)

1. Tool-calling loop con límites: `max_steps`, `max_retries`, timeout, cancel por usuario.
2. Tools de escritura: `apply_text_edits` (mismo shape que LSP edits), opcional create/rename con confirmación.
3. Autofix:
   - **MVP:** iterar sobre diagnostics LSP (sin compile).
   - **Plus (si Q4):** `BuildTaskService` + stderr parse + reintento ≤ 3 + rollback journal.
4. Nivel 2: interfaz lista; implementación del modelo grande **bloqueada a Q6**.

**Criterio de salida:** “arregla el error en la línea del cursor” usa diagnostics + edits + atribución; falla seguro con rollback.

### Fase 4 — RAG vectorial (opcional, Gemini 1.1)

Solo si Q2 lo exige o Fase 3 demuestra huecos:

1. Chunking por nodos Tree-sitter (func/class/struct) — **reutilizar** `tree_sitter_symbols` / AST utils.
2. Store: preferir **sqlite-vec** embebido en un `index.db` bajo cache XDG del workspace hash (menos deps que un motor nuevo).
3. Embeddings micro-modelo local **o** diferir embeddings si el backend LLM ya los ofrece.
4. Búsqueda híbrida: `α·vector + β·rg + γ·LSP symbols`.
5. Cold-start: mientras indexa, solo rg+LSP+TS (ya disponible).

### Fase 5 — UX / producto

- Diff inline de propuestas AI (accept/reject hunk)
- Política de red: strict offline vs endpoint OpenAI-compatible
- Telemetría local de calidad (opcional, off by default)
- Documentación en `docs/user-guide.md` + arquitectura

---

## 6. Mapa explícito: qué NO hacer

1. No reescribir el cliente LSP ni Tree-sitter.
2. No reemplazar `WorkspaceIndexer` / rg por “todo vectorial”.
3. No introducir `.tuide.json` en la raíz.
4. No hacer polling de Git para atribución.
5. No tocar la UI desde hilos de inferencia/compilación.
6. No cargar el modelo en el arranque por defecto (lazy + opt-in).
7. No vender “350 MB pluma” como footprint del IDE.
8. No asumir que `src/build/` ya es un compile runner de agent.
9. No añadir Python como runtime de IA.
10. No implementar Nivel 2 / RAG / llama.cpp hasta cerrar Q1–Q7.

---

## 7. Inventario de APIs internas a reutilizar (checklist de integración)

| Necesidad del agent | API existente |
|---|---|
| Leer/editar texto | `EditorBuffer`, `editor_buffer_joined_source`, apply LSP text edits |
| Undo/rollback | `undo_stack` / `EditorSnapshot` |
| Símbolos / tipos / defs | `ISymbolProvider` (`hover`, `document_symbols`, `workspace_symbols`, definition, refs) |
| Diagnostics | `publishDiagnostics` vía LSP provider + Problems UI |
| Quick-fix no-LLM | code actions ya cableados |
| Parse / outline | `TreeSitterService` |
| Search | `WorkspaceSearch` / `workspace_search_rg` |
| File list | `WorkspaceIndexer` |
| Wake UI | `UI_WAKE`, `UiEventDispatcher`, `Event::Custom` |
| Idle CPU | `UiActivityGate` |
| Config | `.tuide/config.json` / `workspace_config` |
| Cache paths | `$XDG_CACHE_HOME/tuide/…` (`bundled_tools`, captures) |
| Shell / build manual | `ShellSession` PTY |
| Git | `GitService` |

---

## 8. Riesgos técnicos

| Riesgo | Mitigación |
|---|---|
| 0.5B no cumple tool-calling | Router Nivel 0 + eval antes de fijar modelo (Q3) |
| RAM/CPU explotan con llama.cpp | Feature flag OFF; carga lazy; un solo contexto; liberar tras idle |
| Index vectorial stale | Invalidación por mtime + watcher del indexer existente |
| Edits AI rompen sync LSP | Reusar pipeline `didChange` / text edits LSP |
| Autofix loops infinitos | `max_retries=3`, presupuesto tokens, rollback journal |
| Build portable/glibc | No linkear llama por defecto en `build-portable` |
| Alcance Gemini incompleto (corte en 2.2) | Bloquear Fase Nivel 2 hasta recibir el resto del spec |

---

## 9. Entregables de esta rama (`feature/IA`)

- [x] Este documento: contraste crítico + plan refinado (**sin código de producto**).
- [ ] Respuestas a Q1–Q7 (comentario en PR / iteración del spec).
- [ ] Tras decisiones: desglose en issues/checklist de implementación por fase.

---

## 10. Resumen para decisores

Gemini propuso construir un IDE+IA. **tuide ya es el IDE.** El trabajo real es:

1. Capa `src/ai/` con tools sobre APIs existentes.
2. Atribución en memoria (journal), no dual-buffer ni Git polling.
3. LLM local opcional con el mismo espíritu de bundles/XDG.
4. RAG y modelo “grande” solo si hacen falta tras medir.
5. Autofix con límites y rollback, apoyado primero en diagnostics LSP.

Hasta responder **Q1–Q7**, cualquier implementación de llama.cpp/RAG/autofix-compile sería especulación cara.
