# Master Spec: Arquitectura de IA en 2 Niveles + Sistema de Atribución (tuide)

> **Estado:** plan de diseño (sin implementación) — **decisiones de producto incorporadas**.  
> **Origen:** propuesta Gemini contrastada contra el código real de tuide (`main`).  
> **Rama:** `feature/IA` · PR de seguimiento del plan.

---

## 0. Veredicto ejecutivo

tuide **ya es** el IDE TUI (FTXUI, Tree-sitter, ripgrep, multi-LSP, wake idle, buffers, Git, terminal). La IA es una **capa nueva** (`src/ai/` + task runner + atribución), no un IDE desde cero.

Gemini acierta en búnker local, 2 niveles, Search/Replace, atribución en memoria y auto-download. Falla al asumir greenfield y al mezclar router logístico con RAG+LLM en el hot path.

---

## 1. Decisiones cerradas (respuestas del producto)

| # | Tema | Decisión |
|---|---|---|
| D1 | Niveles | **Nivel 0** barrera determinista C++ + **Nivel 1** LLM **más capaz** orientado a tool-calling + **Nivel 2** generador pesado bajo demanda |
| D2 | RAG | **No** en la primera oleada de implementación. **Sí** en el roadmap: contrato de “context pack” listo para enchufar RAG cuando alimente al Nivel 2 |
| D3 | Offline / red | **Capacidad búnker:** todo ejecutable en local. **Escape hatch:** el usuario puede derivar el **Nivel 2** a API remota (DeepSeek / Claude / OpenAI-compatible) |
| D4 | Build / tools | Crear un **Task Runner** real (no confundir con `src/build/` de `compile_commands`) para que Nivel 1 ejecute “compila el proyecto”, tests, scripts |
| D5 | Buffers | **Un solo** `EditorBuffer` + **Edit Journal** por autor. No dual-buffer paralelo |
| D6 | Atribución UI | Journal + (opcional) Myers diff debounce 150 ms para gutter Human vs L1 vs L2 |
| D7 | Nivel 2 | Qwen2.5-Coder-3B-Instruct Q4 (~1.9 GB) local bajo demanda, o remoto; protocolo **Search/Replace**, no archivos enteros |
| D8 | llama.cpp | Como el **resto de bundles** CMake (`TUIDE_BUNDLE_LLAMA` / extracción a `$XDG_CACHE_HOME/tuide/…`), no link obligatorio en el binario slim |

---

## 2. Matriz de contraste (resumen)

| Bloque | ¿Ya existe? | Acción |
|---|---|---|
| FTXUI / `Event::Custom` / `UiActivityGate` | Sí | Reutilizar obligatoriamente |
| Tree-sitter / ripgrep / multi-LSP | Sí | Tools del agent, no reescribir |
| Indexer de archivos/símbolos | Sí (no vectorial) | Cold-start y base pre-RAG |
| `EditorBuffer` + undo | Sí | Extender con journal |
| `.tuide.json` en raíz | **No** — es `.tuide/config.json` | Extender config existente |
| Task runner de compile/test | **No** | **Nuevo** (D4) |
| llama / RAG / agent / atribución | **No** | Nuevo según fases |

Footprint: IDE sigue en ~20–30 MB; modelos se contabilizan **aparte** y lazy.

---

## 3. Arquitectura de niveles (acordada)

```
Usuario (NL / slash / UI)
        │
        ▼
┌───────────────────┐
│ Nivel 0 — Router  │  C++ determinista: intents obvios → tools
│ (sin LLM)         │  open file, rg, hover, diagnostics, git status…
└─────────┬─────────┘
          │ no resuelto / plan multi-tool
          ▼
┌───────────────────┐
│ Nivel 1 — Agent   │  LLM local tool-calling + Task Runner
│ (potente, local)  │  orquesta FS/LSP/search/git/build; decide escalar a L2
└─────────┬─────────┘
          │ razonamiento / rewrite profundo
          ▼
┌───────────────────┐
│ Nivel 2 — Coder   │  Local 3B Q4 **o** API remota (user choice)
│ (bajo demanda)    │  emite Search/Replace; L1 valida y aplica + journal
└───────────────────┘
          │
          ▼  (futuro)
┌───────────────────┐
│ RAG / Context     │  No en v1; misma interfaz `ContextPack` que hoy
│ Pack              │  rellenan LSP+rg+TS (+ embeddings después)
└───────────────────┘
```

### 3.1 Nivel 0 — Barrera determinista

- Entrada: texto del usuario + contexto mínimo (archivo activo, selección, diagnostics visibles).
- Salida: tool call directa **o** “escalate_to_level1”.
- Ejemplos sin LLM: “busca `UiActivityGate`”, “ve a la definición”, “lista errores”, “git status”.
- Objetivo: latencia &lt;100 ms y 0 inferencia para el grueso operativo.

### 3.2 Nivel 1 — Agent tool-calling (más potente que 0.5B)

El 0.5B de Gemini queda **descartado** como cerebro de tools (D1).

**Candidatos a evaluar antes de fijar el GGUF** (benchmark interno de tool-calling JSON + multi-step sobre fixtures tuide):

| Candidato | Tamaño Q4 (aprox.) | Nota |
|---|---|---|
| Qwen2.5-1.5B-Instruct | ~1 GB | Compromiso latencia/calidad |
| Qwen2.5-3B-Instruct | ~1.9 GB | Mejor tools; solapa tamaño con L2 coder |
| Qwen2.5-Coder-1.5B / 3B solo para L1 | varía | Si L1 y L2 comparten familia, unificar descarga |

Nivel 1 **no** sustituye rg/LSP: los llama vía `ToolRegistry`.  
Nivel 1 **sí** posee el Task Runner (compile, test, script custom).

### 3.3 Nivel 2 — Generador pesado (Gemini Fase 3, aceptada con matices)

- Modelo local default propuesto: `Qwen2.5-Coder-3B-Instruct-Q4_K_M.gguf` (~1.9 GB).
- Carga **solo** cuando L1 (o el usuario) activa generación compleja.
- Remoto opcional: OpenAI-compatible / DeepSeek / Claude según config.
- **Protocolo Search/Replace** (no full-file):
  1. L2 emite bloques `SEARCH` / `REPLACE` (o equivalente estructurado).
  2. Motor C++ (Nivel 1 / apply layer) valida que `SEARCH` existe de forma única en el buffer.
  3. Aplica en el **único** `EditorBuffer`, `push_undo`, registra journal `Author::Level2_AI`.
  4. Si `SEARCH` no matchea o es ambiguo → error a L1/L2, no escritura parcial silenciosa.

---

## 4. Atribución: un buffer + Edit Journal (D5, D6)

### 4.1 Modelo de datos (refinado respecto a Gemini)

Gemini propuso:

```cpp
enum class Author { Human, Level1_AI, Level2_AI };

struct TextEdit {
    Author author;
    int start_line;
    int end_line;
    std::string new_text;
    uint64_t timestamp;
};
```

**Problemas del struct tal cual:**

- Solo líneas: las edits de tuide/LSP son rangos `(line, col)` / offsets; perder columna rompe apply preciso y multi-cursor.
- No guarda `old_text` / rango pre-edit → rollback y Myers “por autor” se complican.
- Un vector de `new_text` gigante duplica el rope.

**Contrato recomendado:**

```cpp
enum class Author : uint8_t { Human, Level1_AI, Level2_AI, Lsp /*opcional*/, System };

struct TextEdit {
  Author author;
  uint64_t op_id;           // agrupa un apply AI / undo group
  int start_line, start_col;
  int end_line, end_col;    // rango *antes* del replace (estilo LSP)
  uint32_t new_len;         // o hash; evitar copiar todo el texto si no hace falta
  uint64_t timestamp_ms;
};
```

Más una estructura derivada **por línea** (o por intervalo coalescido) para el gutter: `line → Author` (última escritura gana), invalidada de forma perezosa.

Autores L1 vs L2 permiten UI distinta (p.ej. violeta vs magenta) si se desea; MVP puede colapsar ambos a “AI” en color y distinguir en hover/tooltip.

### 4.2 ¿Myers Diff hace falta? (desacuerdo parcial con Gemini §2.3)

Gemini: debounce 150 ms + Myers(`Base Snapshot`, `Active Buffer`) + cruce con journal → gutter verde/púrpura.

**Posición:**

- La **fuente de verdad de autoría** debe ser el **Journal** (cada tecla/AI apply ya sabe el autor). Eso pinta el gutter **sin** Myers.
- Myers aporta valor para: (a) geometría Added/Modified/Deleted vs **disco** (como un mini git-diff en RAM), (b) reconciliar si el journal se compacta, (c) UI de “diff de sesión”.
- Coste: Myers de archivo grande cada 150 ms en el hilo UI es hostil al idle CPU. Si se hace, **worker + debounce + `UI_WAKE`**, nunca en el hot path de teclado.

**Recomendación de implementación:**

1. MVP atribución: journal → mapa de líneas → gutter (Human / L1 / L2).
2. “Base snapshot” = contenido en el **último save a disco** (ya alineado con `WorkspaceModel` load/save), no un segundo editor.
3. Myers opcional en fase UX, off-UI thread, debounce ≥150 ms, coalesced wake.

Colores propuestos (configurables en theme): Human = verde; L1/L2 = violeta/púrpura (o dos tonos).

### 4.3 Coalescing humano

Igual que `undo_coalesce_open`: tecleo continuo = un `op_id` Human hasta pausa / cambio de cursor lejano / blur.

---

## 5. Task Runner (nuevo — D4)

Distinto de `src/build/` (entornos + `compile_commands` para clangd).

| Pieza | Rol |
|---|---|
| `TaskRunner` / `BuildTaskService` | Subprocess async, stdout/stderr, `exit_code`, cancel |
| Config | En **`.tuide/config.json`**: tasks nombradas (`build`, `test`, …) y/o script |
| Auto-detect | Fallback: `cmake --build build`, `make`, etc. (similar espíritu a detección actual CMake/Make) |
| Tools Nivel 1 | `run_task(name)`, `run_command` (allowlist / confirmación) |
| Autofix loop | L1 aplica edit → `run_task("build")` → si falla, limpia stderr → reintento ≤3 → rollback journal/`undo` |
| UI | Progreso vía eventos + `UI_WAKE`; no pintar desde el hilo del proceso |

Pregunta residual: ¿`run_command` arbitrario está permitido al agent o solo tasks declaradas? (ver Q-A abajo).

---

## 6. RAG: no en v1, sí en el diseño (D2)

Para no pintar contra la pared cuando llegue un L2 “complejo”:

1. Definir desde ya `ContextPack` (archivos, símbolos, diagnostics, snippets, presupuestos de tokens).
2. En v1, el pack lo rellenan **LSP + rg + Tree-sitter + selection + open tabs** (sin embeddings).
3. Más adelante, un `RagProvider` añade chunks vectoriales al **mismo** pack.
4. Chunking futuro: nodos Tree-sitter (ya disponibles), store tipo sqlite-vec bajo `$XDG_CACHE_HOME/tuide/…`.

Así L2 siempre come `ContextPack`; RAG es un backend más, no un rewrite.

---

## 7. Empaquetado llama.cpp (D8)

Alineado a bundles existentes (`BundleClangd`, `BundleRg`, …):

- CMake `TUIDE_BUNDLE_LLAMA` (default **OFF** en builds slim / portable).
- Runtime: blob o download de lib + modelos en `$XDG_CACHE_HOME/tuide/models/` y/o `bundled/`.
- Auto-download de GGUF con progreso FTXUI (mismo espíritu que extracción de tools).
- `build-portable` / glibc: no forzar llama en el artefacto mínimo.

---

## 8. Plan por fases (actualizado)

### Fase 0 — Cimientos sin modelo

1. `ToolRegistry` + tools lectura: FS (vía workspace), search, LSP symbols/hover/diagnostics.
2. Nivel 0 router (heurísticas / slash).
3. Panel AI stub + `UI_WAKE`.
4. Bloque `"ai"` en `.tuide/config.json`.
5. Esqueleto `ContextPack` (sin RAG).

**Done:** preguntas operativas del IDE respondidas sin LLM.

### Fase 1 — Task Runner + atribución journal

1. `TaskRunner` + tasks en config + auto-detect build.
2. Edit Journal + mapa gutter (MVP sin Myers).
3. Apply path único: edits → undo group → journal `Level1_AI`/`Level2_AI` (L2 puede ser stub).
4. Tool `run_task` cableado al panel/agent stub.

**Done:** “compila el proyecto” vía task; gutter Human vs AI tras apply simulado.

### Fase 2 — Nivel 1 LLM local (bundle llama)

1. Backend LLM + ModelStore (download).
2. Elegir GGUF L1 tras eval tool-calling (ver §3.2).
3. Agent loop: tools + max_steps + cancel.
4. Escalado “needs_level2” como tool/result explícito.

**Done:** chat agent offline que usa tools reales del IDE.

### Fase 3 — Nivel 2 + Search/Replace + remoto opcional

1. L2 local 3B coder lazy **o** endpoint remoto (D3).
2. Validador Search/Replace + apply + journal `Level2_AI`.
3. Autofix: diagnostics LSP y/o `run_task("build")`, ≤3 reintentos, rollback.
4. Myers diff opcional (worker) para diff vs disco / UX.

**Done:** “reescribe este algoritmo” produce hunks validados; usuario puede elegir L2 remoto.

### Fase 4 — RAG provider

1. Embeddings + índice; invalidación con indexer/watchers.
2. Inyectar en `ContextPack` sin cambiar el contrato L2.
3. Búsqueda híbrida vector + rg + symbols.

### Fase 5 — UX producto

Accept/reject hunks, theme gutter, docs usuario, i18n, telemetría local off-by-default.

---

## 9. Qué NO hacer

1. Reescribir LSP, Tree-sitter, rg, Git, wake UI.
2. Dual-buffer paralelo al `EditorBuffer`.
3. Git blame/polling para atribución.
4. RAG obligatorio en v1.
5. Qwen 0.5B como cerebro de tool-calling.
6. Devolver archivos enteros desde L2.
7. Myers en el hilo UI en cada tecla.
8. Introducir `.tuide.json` en la raíz.
9. Confundir `src/build/` (compile_commands) con el Task Runner.
10. Python runtime para IA.

---

## 10. Dudas pendientes (nuevas)

Con D1–D8 cerrados, quedan estos puntos antes de codificar:

### Q-A — Modelo concreto del Nivel 1

¿Evalúamos **1.5B Instruct** como default L1, o preferís ir directos a **3B** (más RAM, mejor tools), aceptando solape de tamaño con L2 coder?

### Q-B — Allowlist del Task Runner

Ante “ejecuta lo que haga falta”:

- **A)** Solo tasks declaradas en `.tuide/config.json` (+ auto-detect `build`/`test`).
- **B)** También `run_command` libre con confirmación UI.
- **C)** Libre en workspace sin confirmación (más agente, más riesgo).

Recomendación: **A** en MVP, **B** después.

### Q-C — Colores L1 vs L2 en gutter

¿Dos tonos de violeta, o un solo “AI” + tooltip con el nivel?

### Q-D — Formato exacto Search/Replace

¿Texto estilo Aider (`<<<<<<< SEARCH` / `>>>>>>> REPLACE`), JSON estructurado `{path, search, replace}`, o ambos (L2 remoto a veces prefiere JSON)?

### Q-E — Persistencia del journal

¿Solo RAM mientras el tab está abierto, o sidecar (p.ej. bajo `.tuide/`) para sobrevivir restart sin mezclarse con Git?

### Q-F — Cuándo L0 escala a L1

¿Slash commands (`/build`, `/explain`) fuerzan L0/L1 explícito, o todo el chat pasa por L0 primero siempre?

---

## 11. Inventario de APIs a reutilizar

| Necesidad | API existente |
|---|---|
| Texto / undo | `EditorBuffer`, `undo_stack`, apply estilo LSP edits |
| Símbolos / tipos / diagnostics | `ISymbolProvider`, LSP client |
| Parse / outline | `TreeSitterService` |
| Search / files | `WorkspaceSearch`, `WorkspaceIndexer` |
| Wake / idle | `UI_WAKE`, `UiEventDispatcher`, `UiActivityGate` |
| Config | `.tuide/config.json` |
| Cache | `$XDG_CACHE_HOME/tuide/` (bundled, captures, → models) |
| Shell (no sustituye Task Runner) | `ShellSession` PTY |
| Git | `GitService` |
| compile_commands / clangd env | `src/build/*` (sigue siendo para LSP, no tasks) |

---

## 12. Entregables de esta rama

- [x] Spec contrastado v1
- [x] Decisiones D1–D8 + resto del plan Gemini (§2.2–3.2) integrados
- [ ] Respuestas Q-A … Q-F
- [ ] Tras eso: issues/checklist de implementación por fase (sigue sin código de producto hasta acuerdo)
