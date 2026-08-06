# Master Spec: Arquitectura de IA en 2 Niveles + Sistema de Atribución (tuide)

> **Estado:** plan de diseño (sin implementación) — **decisiones D1–D15 cerradas**.  
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
| D6 | Atribución UI | Journal (+ Myers opcional debounce 150 ms). Gutter: **humano = verde**, **AI = azul** (D11) |
| D7 | Nivel 2 | Qwen2.5-Coder-3B-Instruct Q4 (~1.9 GB) local bajo demanda, o remoto; protocolo **Search/Replace**, no archivos enteros |
| D8 | llama.cpp | Como el **resto de bundles** CMake (`TUIDE_BUNDLE_LLAMA` / extracción a `$XDG_CACHE_HOME/tuide/…`), no link obligatorio en el binario slim |
| D9 | Modelo L1 | Default **Qwen2.5-3B-Instruct Q4** (tool-calling). Puede compartir familia/descarga con L2 coder 3B; si en la práctica un solo 3B-Coder cubre agent+codegen, unificar más adelante |
| D10 | Task Runner / shell | Solo comandos en una **whitelist** (config + defaults). Por defecto entran **compile** y **launch** (auto-detect / tasks del proyecto). Fuera de la lista → **no se ejecuta**; la UI puede ofrecer “añadir a la whitelist” de forma explícita |
| D11 | Gutter | Un color **AI** (p.ej. **azul**), distinto del humano (verde). No hace falta tono distinto L1 vs L2 en MVP; el nivel puede ir en tooltip |
| D12 | Search/Replace | Formato **estilo Aider** adaptado a C++ (bloques SEARCH/REPLACE en texto). Fácil de validar y de depurar; JSON como transporte opcional solo si un backend remoto lo exige |
| D13 | Journal persistente | **Sidecar** bajo `.tuide/` (por path/archivo) para sobrevivir al reabrir el proyecto; no mezclar con Git |
| D14 | Enrutado L0 | **Todo** el chat pasa primero por Nivel 0 (puerta barata). L0 resuelve o escala a L1. Slash commands son atajos L0 (p.ej. `/build` → task runner sin LLM) |
| D15 | UI chat | Tab **AI** en el panel inferior (`ConsolePanelTabs`), junto a Terminal / Problems / Git / …. Transcript tipo terminal/chat (estilo Cursor): respuestas de texto, tool/status y **stdout/stderr de comandos** del Task Runner. Los cambios de código se aplican **en el editor** (auto + journal/gutter), no se vuelcan como archivos enteros en el chat |

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

### 3.1 Nivel 0 — Barrera determinista (siempre primero — D14)

**Por qué conviene pasar todo por L0** (frente a mandar el chat directo a L1):

1. Latencia y CPU: “compila”, “busca X”, “ve a la definición” no necesitan 3B.
2. Determinismo: menos alucinaciones de paths/comandos en lo operativo.
3. Cancelación clara: L0 puede mapear slash → tool sin gastar tokens.
4. Mismo tubo de entrada: un solo panel de chat; L0 decide `resolve` vs `escalate_to_level1`.

Flujo:

- Entrada: texto del usuario + contexto mínimo (archivo activo, selección, diagnostics visibles).
- Salida: tool call directa **o** `escalate_to_level1` (con el mensaje intacto + hints).
- Slash (`/build`, `/test`, `/explain`…): atajos L0; `/explain` tipicamente escala a L1/L2.
- Confianza baja / NL ambiguo → L1 siempre.

### 3.2 Nivel 1 — Agent tool-calling (3B — D9)

Default: **Qwen2.5-3B-Instruct Q4** (descartado 0.5B/1.5B como cerebro de tools).

Nota de empaquetado: L2 propone `Qwen2.5-Coder-3B`. Son pesos distintos (~mismo orden de GB). Opciones:

1. **Dos GGUF** (Instruct agent + Coder generate) — más calidad por rol, más disco.
2. **Unificar** en Coder-3B para ambos si el eval de tool-calling es aceptable — menos descarga.

Decisión de producto ahora: **partir con Instruct 3B en L1**; medir si Coder-3B puede sustituirlo antes de GA. No bloquear el diseño por unificar aún.

Nivel 1 **no** sustituye rg/LSP: los llama vía `ToolRegistry`.  
Nivel 1 **sí** posee el Task Runner (compile, launch y demás **solo si están en la whitelist** — D10).

### 3.3 Nivel 2 — Generador pesado (Gemini Fase 3, aceptada con matices)

- Modelo local default propuesto: `Qwen2.5-Coder-3B-Instruct-Q4_K_M.gguf` (~1.9 GB).
- Carga **solo** cuando L1 (o el usuario) activa generación compleja.
- Remoto opcional: OpenAI-compatible / DeepSeek / Claude según config.
- **Protocolo Search/Replace (D12), estilo Aider adaptado a C++:**
  1. L2 emite bloques texto `SEARCH` / `REPLACE` (gramática compatible con la de Aider; parser propio en C++).
  2. Motor C++ valida que `SEARCH` existe de forma **única** en el buffer (o path indicado).
  3. Aplica en el **único** `EditorBuffer`, `push_undo`, journal `Author::Level2_AI`.
  4. Si no matchea o es ambiguo → error a L1/L2, sin escritura parcial.
  5. JSON `{path,search,replace}` solo como adaptador si un proveedor remoto no habla Aider-text.

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

Autores L1 vs L2 se guardan en el journal (telemetry / tooltip). **Gutter MVP (D11):** humano = verde; cualquier AI = **azul**. Sin segundo tono L1/L2.

### 4.2 ¿Myers Diff hace falta? (desacuerdo parcial con Gemini §2.3)

Gemini: debounce 150 ms + Myers(`Base Snapshot`, `Active Buffer`) + cruce con journal → gutter verde/púrpura.

**Posición:**

- La **fuente de verdad de autoría** debe ser el **Journal** (cada tecla/AI apply ya sabe el autor). Eso pinta el gutter **sin** Myers.
- Myers aporta valor para: (a) geometría Added/Modified/Deleted vs **disco** (como un mini git-diff en RAM), (b) reconciliar si el journal se compacta, (c) UI de “diff de sesión”.
- Coste: Myers de archivo grande cada 150 ms en el hilo UI es hostil al idle CPU. Si se hace, **worker + debounce + `UI_WAKE`**, nunca en el hot path de teclado.

**Recomendación de implementación:**

1. MVP atribución: journal → mapa de líneas → gutter (Human verde / AI azul).
2. “Base snapshot” = contenido en el **último save a disco** (ya alineado con `WorkspaceModel` load/save), no un segundo editor.
3. Myers opcional en fase UX, off-UI thread, debounce ≥150 ms, coalesced wake.

### 4.3 Coalescing humano

Igual que `undo_coalesce_open`: tecleo continuo = un `op_id` Human hasta pausa / cambio de cursor lejano / blur.

### 4.4 Sidecar de persistencia (D13)

- Ruta tentativa: `.tuide/ai/attribution/<hash-or-relpath>.json` (o un store compacto binario si el JSON crece).
- Se escribe debounce al save del buffer / al cerrar tab / al salir.
- Al reabrir: cargar journal → repintar gutter; si el fichero en disco no coincide con el hash guardado, **invalidar** (no mentir en el gutter).
- `.tuide/` ya es el lugar de config del workspace; añadir a reglas de skip del indexer si hace falta.
- **No** commitear atribución a Git por defecto (documentar; opcional en `.gitignore` del usuario).

---

## 5. Task Runner (nuevo — D4, D10)

Distinto de `src/build/` (entornos + `compile_commands` para clangd).

| Pieza | Rol |
|---|---|
| `TaskRunner` / `BuildTaskService` | Subprocess async, stdout/stderr, `exit_code`, cancel |
| Config | En **`.tuide/config.json`**: tasks nombradas + **`ai.command_whitelist`** (patrones o argv exactos) |
| Defaults de whitelist | Al menos **compile** y **launch** (auto-detect CMake/Make / binario del wizard). El usuario amplía la lista (p.ej. `ctest`, `./tools/…`) |
| Tools Nivel 1 | `run_task(name)`, `run_command(cmdline)` **solo si pasa el matcher de whitelist** |
| Política (D10) | **No hay shell libre.** Comando no listado → rechazo + mensaje; opcional modal “¿Añadir este comando a la whitelist del workspace?” (nunca ejecución silenciosa) |
| Matching | Preferir argv tokenizado / prefijos controlados (evitar que `make` autorice `make clean && rm -rf /` por substring ingenuo) |
| Autofix loop | L1 aplica edit → `run_task("build")` (en whitelist) → stderr → reintento ≤3 → rollback |
| UI | Progreso vía `UI_WAKE`; gestión de whitelist en settings / modal |

La PTY del terminal integrado **no** sustituye al Task Runner: el usuario sigue pudiendo escribir lo que quiera en la shell; el **agent** no. La salida de comandos del agent va al tab **AI** (D15), no a la PTY (salvo que el usuario los teclee él mismo en Terminal).

---

## 6. UI: tab AI en el panel de consola (D15)

### 6.1 Dónde vive

El panel inferior ya es un host de tabs (`ConsolePanelTabs` en `src/ui/main_layout.hpp`): Terminal, App, Debug, Performance, Problems, Search, Call Hierarchy, Git, Core Analyzer, Binary Symbols, Packet Monitor.

**Añadir** `kAi` (nombre UI: **AI** / i18n) como una tab más. Misma geometría, foco `FocusRegion::Terminal` / input focus de consola, atajo opcional (p.ej. dedicarlo cuando el agent arranca o con un keybind).

No abrir un panel lateral nuevo ni un modal de chat a pantalla completa: rompe el layout actual y compite con Outline/Search.

### 6.2 Qué se muestra (transcript tipo terminal)

Scrollback de líneas/eventos, sensación de “salida de consola + chat”, no un feed de cards:

| Evento | En el tab AI | En el editor |
|---|---|---|
| Mensaje del usuario | Sí (prompt) | — |
| Respuesta NL de L0/L1/L2 | Sí (streaming si aplica) | — |
| Tool call / status (“search…”, “compile…”) | Sí (línea de log breve) | — |
| `run_task` / `run_command` (whitelist) | **Sí: stdout/stderr + exit code** | — |
| Apply Search/Replace / edits | Log breve (“applied N hunks in foo.cpp”) | **Texto aplicado** + journal + gutter azul |
| Deny whitelist | Sí (motivo + oferta “añadir a whitelist”) | — |
| Error / cancel | Sí | — |

Los diffs grandes **no** se pegan enteros en el chat; el usuario los ve en el buffer (y más adelante accept/reject hunk si se añade en Fase 5).

### 6.3 Input

- Línea de entrada abajo del transcript (como el input de Debug/GDB o un prompt de chat).
- Slash commands (`/build`, …) y NL libre → siempre L0 primero (D14).
- Separado de la PTY: escribir en AI **no** envía bytes al bash embebido.

### 6.4 Relación con Terminal PTY

| | Tab Terminal | Tab AI |
|---|---|---|
| Quién escribe comandos | Usuario (shell real) | Agent vía Task Runner (whitelist) |
| Salida | PTY | Transcript AI |
| Edits de código | No | Dispara apply en editor |

### 6.5 Wake / idle

Appends al transcript solo vía cola + `UI_WAKE`; respetar `UiActivityGate` (no spamear frames en idle si no hay texto nuevo). Streaming de tokens: coalescer updates.

---

## 7. RAG: no en v1, sí en el diseño (D2)

Para no pintar contra la pared cuando llegue un L2 “complejo”:

1. Definir desde ya `ContextPack` (archivos, símbolos, diagnostics, snippets, presupuestos de tokens).
2. En v1, el pack lo rellenan **LSP + rg + Tree-sitter + selection + open tabs** (sin embeddings).
3. Más adelante, un `RagProvider` añade chunks vectoriales al **mismo** pack.
4. Chunking futuro: nodos Tree-sitter (ya disponibles), store tipo sqlite-vec bajo `$XDG_CACHE_HOME/tuide/…`.

Así L2 siempre come `ContextPack`; RAG es un backend más, no un rewrite.

---

## 8. Empaquetado llama.cpp (D8)

Alineado a bundles existentes (`BundleClangd`, `BundleRg`, …):

- CMake `TUIDE_BUNDLE_LLAMA` (default **OFF** en builds slim / portable).
- Runtime: blob o download de lib + modelos en `$XDG_CACHE_HOME/tuide/models/` y/o `bundled/`.
- Auto-download de GGUF con progreso FTXUI (mismo espíritu que extracción de tools).
- `build-portable` / glibc: no forzar llama en el artefacto mínimo.

---

## 9. Plan por fases (actualizado)

### Fase 0 — Cimientos sin modelo

1. `ToolRegistry` + tools lectura: FS (vía workspace), search, LSP symbols/hover/diagnostics.
2. Nivel 0 router (heurísticas / slash).
3. Tab **AI** stub en `ConsolePanelTabs` + input + `UI_WAKE` (D15).
4. Bloque `"ai"` en `.tuide/config.json`.
5. Esqueleto `ContextPack` (sin RAG).

**Done:** transcript operativo en la consola inferior; tools de lectura sin LLM.

### Fase 1 — Task Runner + atribución journal

1. `TaskRunner` + tasks en config + auto-detect build/launch.
2. Whitelist de comandos (defaults compile/launch + ampliación en config; modal “añadir”) (D10).
3. Edit Journal + mapa gutter Human/AI + **sidecar `.tuide/`** (D13).
4. Apply path único: edits → undo group → journal.
5. Tools `run_task` / `run_command` (gateados por whitelist); su salida va al **transcript del tab AI** (D15).

**Done:** “compila el proyecto” si está en whitelist (log en tab AI); “curl …” se rechaza (o pide añadir a whitelist); gutter azul/verde sobrevive reopen.

### Fase 2 — Nivel 1 LLM local (bundle llama)

1. Backend LLM + ModelStore (download).
2. GGUF L1: **Qwen2.5-3B-Instruct Q4** (D9).
3. Agent loop: tools + max_steps + cancel + escalate L2.
4. L0 siempre delante del chat (D14); slash → L0.

**Done:** chat agent offline que usa tools reales del IDE.

### Fase 3 — Nivel 2 + Search/Replace + remoto opcional

1. L2 local Coder-3B lazy **o** endpoint remoto (D3).
2. Parser/validador **estilo Aider** (D12) + apply + journal `Level2_AI`.
3. Autofix: diagnostics LSP y/o `run_task("build")`, ≤3 reintentos, rollback.
4. Myers diff opcional (worker) para diff vs disco / UX.

**Done:** “reescribe este algoritmo” produce hunks validados; L2 remoto opcional.

### Fase 4 — RAG provider

1. Embeddings + índice; invalidación con indexer/watchers.
2. Inyectar en `ContextPack` sin cambiar el contrato L2.
3. Búsqueda híbrida vector + rg + symbols.

### Fase 5 — UX producto

Accept/reject hunks (opcional), theme gutter azul, docs, i18n, atajo para enfocar tab AI, telemetría local off-by-default.

---

## 10. Qué NO hacer

1. Reescribir LSP, Tree-sitter, rg, Git, wake UI.
2. Dual-buffer paralelo al `EditorBuffer`.
3. Git blame/polling para atribución.
4. RAG obligatorio en v1.
5. Qwen 0.5B como cerebro de tool-calling.
6. Devolver archivos enteros desde L2.
7. Myers en el hilo UI en cada tecla.
8. Introducir `.tuide.json` en la raíz.
9. Confundir `src/build/` (compile_commands) con el Task Runner.
10. Panel de chat lateral o flotante aparte del host de tabs de consola (D15: tab **AI** abajo).
11. Volcar archivos enteros o diffs enormes en el transcript (el código va al editor).
12. Enviar keystrokes del tab AI a la PTY del Terminal.

---

## 11. Decisiones Q-A … Q-F (cerradas)

| Id | Pregunta | Resolución |
|---|---|---|
| Q-A | Modelo L1 | **3B** Instruct Q4 por defecto (**D9**) |
| Q-B | Shell del agent | **Whitelist** de comandos; compile/launch por defecto; fuera = deny / “añadir a whitelist” (**D10**) |
| Q-C | Color gutter | AI = **azul**; humano = verde (**D11**) |
| Q-D | Formato S/R | **Aider-text** adaptado a C++; JSON solo si hace falta (**D12**) |
| Q-E | Persistencia | **Sidecar** en `.tuide/` (**D13**) |
| Q-F | ¿Todo por L0? | **Sí** — más conveniente (latencia, CPU, determinismo); slash = atajos L0 (**D14**) |

### Dudas menores restantes (no bloquean el diseño)

1. ¿Unificar L1 Instruct-3B y L2 Coder-3B en un solo GGUF tras un eval corto, o mantener dos roles?
2. ¿La whitelist se edita solo en `.tuide/config.json` o también desde un panel de settings en la TUI?
3. ¿El azul AI del gutter reutiliza un token del theme actual o se añade `theme.ai_gutter`?

---

## 12. Inventario de APIs a reutilizar

| Necesidad | API existente |
|---|---|
| Texto / undo | `EditorBuffer`, `undo_stack`, apply estilo LSP edits |
| Símbolos / tipos / diagnostics | `ISymbolProvider`, LSP client |
| Parse / outline | `TreeSitterService` |
| Search / files | `WorkspaceSearch`, `WorkspaceIndexer` |
| Wake / idle | `UI_WAKE`, `UiEventDispatcher`, `UiActivityGate` |
| Config | `.tuide/config.json` |
| Cache | `$XDG_CACHE_HOME/tuide/` (bundled, captures, → models) |
| Shell (no sustituye Task Runner) | `ShellSession` PTY — tab Terminal |
| Host de tabs inferior | `ConsolePanelTabs` / `ConsolePanel` — añadir `kAi` |
| Git | `GitService` |
| compile_commands / clangd env | `src/build/*` (sigue siendo para LSP, no tasks) |

---

## 13. Entregables de esta rama

- [x] Spec contrastado v1
- [x] Decisiones D1–D8 + resto del plan Gemini (§2.2–3.2)
- [x] Decisiones Q-A … Q-F → D9–D14
- [x] UI chat = tab en consola inferior (**D15**)
- [ ] (Opcional) Checklist de issues por fase cuando se abra implementación
- [ ] Sin código de producto en esta rama hasta que se pida explícitamente
