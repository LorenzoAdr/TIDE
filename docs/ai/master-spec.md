# Master Spec: Arquitectura de IA en 2 Niveles + Sistema de Atribución (tuide)

> **Estado:** plan de diseño (sin implementación) — **decisiones D1–D18 cerradas**.  
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
| D7 | Nivel 2 (código) | **Qwen2.5-Coder-7B-Instruct** Q4 (Apache, codegen). Alt Coder-1.5B. Evitar Coder-3B (no comercial) y no usar Instruct genérico como L2 por defecto |
| D8 | llama.cpp | Bundle CMake; runtime **MIT**. Pesos según **D18** (redistribuibles) |
| D9 | Nivel 1 (tools) | Default **ligero:** **Qwen2.5-1.5B-Instruct** Q4 (~1 GB, Apache) — tools/seeds con L0 delante. **Upgrade** si falla eval: **Phi-4-mini-instruct** (~2.5 GB, MIT) o Qwen2.5-7B-Instruct. **No** Coder como L1. Evitar Qwen2.5-3B (no comercial). El “3B” inicial era techo orientativo; con L0+L2 ya no hace falta ese peso por defecto |
| D10 | Task Runner / shell | Solo comandos en una **whitelist** (config + defaults). Por defecto entran **compile** y **launch** (auto-detect / tasks del proyecto). Fuera de la lista → **no se ejecuta**; la UI puede ofrecer “añadir a la whitelist” de forma explícita |
| D11 | Gutter | Un color **AI** (p.ej. **azul**), distinto del humano (verde). No hace falta tono distinto L1 vs L2 en MVP; el nivel puede ir en tooltip |
| D12 | Search/Replace | Formato **estilo Aider** adaptado a C++ (bloques SEARCH/REPLACE en texto). Fácil de validar y de depurar; JSON como transporte opcional solo si un backend remoto lo exige |
| D13 | Journal persistente | **Sidecar** bajo `.tuide/` (por path/archivo) para sobrevivir al reabrir el proyecto; no mezclar con Git |
| D14 | Enrutado L0 | **Todo** el chat pasa primero por Nivel 0 (puerta barata). L0 resuelve o escala a L1. Slash commands son atajos L0 (p.ej. `/build` → task runner sin LLM) |
| D15 | UI chat | Tab **AI** en el panel inferior (`ConsolePanelTabs`), junto a Terminal / Problems / Git / …. Transcript tipo terminal/chat (estilo Cursor): respuestas de texto, tool/status y **stdout/stderr de comandos** del Task Runner. Los cambios de código se aplican **en el editor** (auto + journal/gutter), no se vuelcan como archivos enteros en el chat |
| D16 | Roadmap por etapas | Implementar en orden estricto: **Bases → L0 → L1 → L2-dry-run → L2**. Antes del modelo L2, el Nivel 1 **imprime en el tab AI** el paquete que enviaría a L2 (`ContextPack` + instrucción) para validar fragmentos y prompts a ojo |
| D18 | Licencias IA | Criterio práctico: la licencia debe **permitir redistribuir los pesos en GitHub Releases** y que la app los descargue/use. No hace falta que sea MIT/Apache. **Rechazar** si: prohíbe redistribución, es **solo no-comercial** (p.ej. Qwen Research), o impone copyleft viral tipo **GPLv3** sobre el binario de tuide. **Aceptable** con condiciones de atribución: MIT, Apache-2.0, Llama Community (“Built with Llama” + NOTICE), etc. |

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
│ L2 dry-run (D16)  │  L1 imprime en tab AI el payload (pack + instrucción)
│ luego L2 real     │  Coder-7B Apache Q4 **o** API remota; Search/Replace
└───────────────────┘
          │
          ▼  (futuro)
┌───────────────────┐
│ ContextPack (D17) │  rg → TS bodies → LSP + incoming calls → headers
│ (+ RAG en Fase F) │  mismo contrato para dry-run y L2
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

### 3.2 Nivel 1 — Agent / tools (D9, D18)

**Rol L1 (no es un coder):** tool-calling, QuerySeeds (D17), escalado a L2, resumir diagnostics/tasks. Métrica = **fiabilidad de tools/JSON/planes**, no HumanEval.

**Revisión de potencia:** el techo “~3B” (Q-A antigua) tenía sentido cuando L1 parecía el cerebro único. Con **L0** + **L2 coder**, L1 es un **traductor/orquestador**. Un **1.5B-class** suele bastar con grammar/JSON constrained; subir de peso solo si el eval de seeds/tools falla.

| Tier | Modelo L1 | Licencia | Q4 | Cuándo |
|---|---|---|---|---|
| **Default (ligero)** | **Qwen2.5-1.5B-Instruct** | Apache-2.0 | ~1.0 GB | Partir aquí (Releases/RAM) |
| Upgrade | **Phi-4-mini-instruct** | MIT | ~2.5 GB | Si 1.5B falla tools/seeds en fixtures tuide |
| Potencia | Qwen2.5-7B-Instruct | Apache-2.0 | ~4.5 GB | Solo si hace falta más techo de agent |
| Alt | Llama-3.2-3B-Instruct | Llama Community | ~2.0 GB | Con “Built with Llama” + NOTICE |
| ~~Coder como L1~~ | — | — | — | **No:** rol incorrecto |
| ~~Qwen2.5-3B / 0.5B~~ | Research / débil | — | — | 3B no comercial; 0.5B frágil para seeds conceptuales |

Siempre: **decoding acotado** (JSON schema / grammar). L1 usa `ToolRegistry` + Task Runner; **no** sustituye rg/LSP.

**Criterio de subida de tier:** en Fase C medir (a) tool calls válidos multi-paso, (b) calidad de QuerySeeds en frases tipo §7.0. Si falla → Phi-4-mini; si aún falla → 7B.

### 3.3 Nivel 2 — Generador de código (D7, D18)

**Rol L2:** codegen / rewrites / Search-Replace de calidad. Métrica = **código**, no chat general.

Default: **Qwen2.5-Coder-7B-Instruct** Q4_K_M (~4.5 GB, Apache-2.0).

| Candidato L2 | Licencia | Releases | Q4 | Encaje rol código |
|---|---|---|---|---|
| **Qwen2.5-Coder-7B-Instruct** (default) | Apache-2.0 | Sí | ~4.5 GB | Especializado código |
| Qwen2.5-Coder-1.5B-Instruct | Apache-2.0 | Sí | ~1.0 GB | Tier ligero |
| ~~Qwen2.5-Coder-3B~~ | Qwen Research | No | ~1.9 GB | No comercial |
| ~~Phi / Instruct genérico como L2~~ | — | — | — | **No default:** inferior a Coder-7B en edits |
| Remoto (opt-in) | API | N/A | 0 | DeepSeek / Claude / OpenAI-compatible |

**Dos assets a propósito:** L1=instruct/agent, L2=coder. No unificar en un solo modelo salvo hardware extremo.

Carga L2 solo bajo demanda. Remoto opcional (D3).

**Protocolo Search/Replace (D12), estilo Aider adaptado a C++:**

1. L2 emite bloques texto `SEARCH` / `REPLACE` (parser C++ propio).
2. Validar que `SEARCH` existe de forma **única** en el buffer.
3. Apply en el único `EditorBuffer`, `push_undo`, journal `Author::Level2_AI`.
4. Si no matchea o es ambiguo → error a L1/L2, sin escritura parcial.
5. JSON `{path,search,replace}` solo como adaptador si un proveedor remoto lo exige.

### 3.4 Empaquetado en GitHub Releases (D18)

Criterio legal (no es “solo MIT/Apache”):

1. ¿Puedo **subir el GGUF** a un Release de este repo y que la app lo descargue?
2. ¿Pueden usarlo usuarios (incl. en empresas) sin violar “non-commercial only”?
3. ¿La licencia **no contagia** GPLv3 al binario `tuide`? (llama.cpp = MIT → OK)

| Tipo | Ejemplos | Veredicto |
|---|---|---|
| MIT / Apache-2.0 | Phi-4-mini, Qwen2.5-1.5B/7B/Coder-7B | Ideal |
| Llama Community | Llama-3.2-3B | OK si se cumple atribución (“Built with Llama”, NOTICE, copia de licencia) |
| Qwen Research | Qwen2.5-**3B**, Coder-**3B** | **No** para Releases de producto (cláusula no-comercial) |
| GPLv3 (código/runtime) | — | Evitar en la cadena que enlace con `tuide` |

Assets: `tuide-model-l1-….gguf` + licencia/NOTICE + checksum → `$XDG_CACHE_HOME/tuide/models/`.

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

## 7. RAG: no en v1; ContextPack “estructural” (D2, D17)

RAG vectorial queda en **Fase F**. Hasta entonces el contexto para L1/L2 (y el dry-run) se arma con el **IDE que ya tenemos**: ripgrep + Tree-sitter + LSP (incl. call hierarchy) + headers relevantes.

### 7.0 El problema real: ¿quién dicta lo que busca ripgrep?

`rg` no entiende  
“oye búscame dónde se haga la construcción de la estructura que enviamos a X sistema”.

**Partir esa frase y grepear `construcción` / `estructura` / `enviamos` / `sistema` no basta** (y suele ser peor): en el repo vivirán nombres tipo `PayloadBuilder`, `MakeXRequest`, `serialize_order()`, `XClient::send()` — vocabulario de **código**, no de la frase en español.

Sin embeddings hace falta un paso explícito **intent → seeds de código**. Eso no lo hace `rg`.

#### Quién dicta los seeds

| Situación | Quién dicta | Qué ocurre |
|---|---|---|
| Cursor/selección en un símbolo | **Editor (S0)** | Ancla directa; a menudo ni hace falta rg |
| El mensaje ya trae ids/paths (`` `OrderPayload` ``, `foo.cpp`) | **L0 extract (S1)** | Solo literales; no “comprende” la frase |
| Frase conceptual en NL (el ejemplo de arriba) | **L1 (S4) — obligatorio** | L1 **traduce** a identificadores/plan de búsqueda |
| L1 no conoce el argot interno del repo | **RAG (S5, Fase F)** | Similitud semántica sobre chunks |

**Regla dura:** si S0/S1 no dan ancla léxica clara → **prohibido** lanzar `rg` sobre tokens de la frase del usuario. Escalar a **L1** para que dicte seeds.

#### Qué debe emitir L1 (no es grep de la frase)

```text
intent: find where we build the struct/payload sent to system X
seeds_primary: ["X", "XClient", "XApi", "send_to_X", "XRequest"]
seeds_structural: ["Builder", "make_", "build_", "serialize", "encode", "Payload", "Request"]
search_plan:
  1. anclar dominio X (workspace_symbols / rg de seeds_primary)
  2. sobre tipos/archivos hallados → buscar construcción (Builder/make_/serialize…)
  3. expandir con TS bodies + call hierarchy + headers
reject_as_noise: ["construcción", "estructura", "enviamos", "búscame", "dónde"]
```

L1 **inventa candidatos de código** y un **plan multi-paso**; el runtime solo ejecuta tools. Los seeds que no confirmen `workspace_symbols`/rg se **descartan** (anti-alucinación).

#### Por qué multi-paso

Una sola query no cubre el ejemplo: primero localizar “sistema X”, luego “construcción del payload” en ese vecindario, luego callers/headers. El dry-run (D16) debe mostrar seeds + plan + hits para ver si falló la **traducción** o la **expansión**.

#### Capas QuerySeed (actualizado)

| Capa | Quién | Rol |
|---|---|---|
| **S0** | Editor | Cursor / selección |
| **S1** | L0 | Solo literales del mensaje |
| **S2** | — | **No usar** tokenizar/grepear la frase NL como estrategia |
| **S3** | LSP | `workspace_symbols` para **confirmar** seeds |
| **S4** | L1 | Dicta seeds de código + plan (caso conceptual) |
| **S5** | RAG Fase F | Cuando S4 no recupera nombres internos |

**Orden:** ancla editor → literales L0 → si NL conceptual **L1 dicta seeds** → rg/LSP solo con seeds confirmados → pack; si sigue pobre → RAG más adelante.

### 7.1 Pipeline completo (seed → pack)

```
Intent usuario
        │
        ├─0─ QuerySeed (S0…S4) → lista de términos / símbolos ancla
        │
        ├─1─ Por cada seed:
        │      · LSP workspace_symbols / definition (preferente)
        │      · ripgrep word/regex acotado (WorkspaceSearch)
        │
        ├─2─ Tree-sitter → cuerpo semántico del hit
        │      (function / method / class / struct)
        │
        ├─3─ LSP en el símbolo ancla
        │      · hover / signature / definition
        │      · references (muestra acotada)
        │      · call hierarchy incoming (+ outgoing si aporta)
        │
        ├─4─ Headers / includes de relevancia (declaración + directos)
        │
        └─5─ Presupuesto de tokens → ContextPack
```

Sustituto de RAG en A–E = **L1 (o anclas literales) dicta seeds de código** + rg/LSP de confirmación + extracción estructural + grafo de llamadas. **No** es “grep de las palabras del prompt”.

### 7.2 Qué va en el pack (prioridad)

| Prioridad | Contenido | Fuente en tuide |
|---|---|---|
| P0 | Selección del usuario + archivo/cursor activos | Editor |
| P0 | Diagnostics del archivo (y relacionados) | LSP `publishDiagnostics` |
| P1 | Seeds usados + justificación breve (para dry-run) | L0/L1 |
| P1 | Cuerpos TS de hits (no solo la línea) | `TreeSitterService` |
| P1 | Definición + firma del símbolo | LSP hover / definition |
| P2 | **Incoming** call hierarchy | LSP call hierarchy |
| P2 | Header/declaración + includes directos | LSP + includes / `include_tree` |
| P3 | Outgoing / refs extra / tabs vecinos | Solo si cabe presupuesto |

### 7.3 Headers: sí, pero con freno

**Sí pasar headers de relevancia**, no el closure completo del `#include` graph:

- Declaración del símbolo si está en header.
- Includes **directos** del `.cpp`/archivo ancla que mencionan el tipo/símbolo, o el header “home” del símbolo.
- Evitar volcar libc/STL/system headers salvo que el hit sea ahí.
- Si el presupuesto aprieta: header de declaración > includes directos > transitivos.

### 7.4 Call hierarchy: “¿de dónde se llama?”

Para codegen/autofix suele importar más **incoming** (callers) que outgoing. Default del pack:

1. Incoming (hasta N callers, con snippet TS del call-site).
2. Outgoing solo si L1 lo pide explícitamente o el intent es “entender dependencias hacia abajo”.

### 7.5 Relación con dry-run (D16)

El tab AI debe mostrar **también los seeds** (“busqué: `connect`, `ConnectionManager`, …”) + hits + cuerpos + callers. Así se ve si el fallo fue **mala query** (capa seed) o **mala expansión** (TS/LSP).

### 7.6 Cuando llegue RAG (Fase F)

Ahí sí la frase conceptual puede recuperar chunks sin pasar por keywords perfectas. `RagProvider` aporta candidatos al mismo `ContextPack`; **no** elimina S0–S4 (siguen siendo más precisos cuando hay un símbolo claro).

---

## 8. Empaquetado llama.cpp (D8)

Alineado a bundles existentes (`BundleClangd`, `BundleRg`, …):

- CMake `TUIDE_BUNDLE_LLAMA` (default **OFF** en builds slim / portable).
- Runtime: blob o download de lib + modelos en `$XDG_CACHE_HOME/tuide/models/` y/o `bundled/`.
- Auto-download de GGUF con progreso FTXUI (mismo espíritu que extracción de tools).
- `build-portable` / glibc: no forzar llama en el artefacto mínimo.

---

## 9. Plan por fases (orden estricto — D16)

Principio: **no saltar etapas**. Cada fase desbloquea la siguiente con un criterio de salida comprobable. El modelo pesado (L2) es lo último; antes hay un estadio de **dry-run** donde L1 solo muestra qué mandaría a L2.

```
Fase A Bases ──► B Nivel 0 ──► C Nivel 1 ──► D L2 dry-run ──► E Nivel 2
                                                              │
                                         (luego) RAG / UX ────┘
```

### Fase A — Bases (sin router NL completo, sin LLM)

Infraestructura compartida por todos los niveles.

1. Tab **AI** en `ConsolePanelTabs` + transcript scrollback + línea de input + `UI_WAKE` (D15).
2. Bloque `"ai"` en `.tuide/config.json` (enabled, paths, whitelist vacía/defaults).
3. `ToolRegistry` + tools de **lectura**: FS workspace, search/rg, LSP symbols/hover/diagnostics.
4. `TaskRunner` + **whitelist** (compile/launch por defecto) + volcado stdout/stderr al tab AI (D10).
5. Edit Journal + apply path (undo group) + gutter Human/AI + sidecar `.tuide/` (D5, D11, D13).
6. Esqueleto `ContextPack` + ensamblador estructural (rg → TS bodies → LSP/call hierarchy → headers) sin embeddings (**D17**).

**Done:** desde el tab AI se pueden invocar tools/tasks; un apply simulado pinta gutter azul; se puede pedir un pack de contexto y ver hits+cuerpos+callers en transcript/dump.

**No incluye:** heurísticas NL de L0, ni llama.cpp, ni L2.

### Fase B — Nivel 0 (router determinista)

1. Clasificador/heurísticas + slash (`/build`, `/search`, `/diag`, …).
2. Todo input del tab AI entra por L0 (D14).
3. `resolve` → tool/task directa; `escalate_to_level1` → mensaje claro en transcript (“requiere Nivel 1; aún no cargado” mientras C no exista).

**Done:** “compila”, “busca X”, “lista errores”, “git status” (si hay tool) funcionan **sin** modelo. NL ambiguo se escala con log explícito.

### Fase C — Nivel 1 (agent LLM local)

1. Bundle/backend llama.cpp + ModelStore (D8, D18).
2. GGUF L1 default: **Qwen2.5-1.5B-Instruct** Q4 (D9); upgrade Phi-4-mini / Qwen-7B si el eval lo pide.
3. Agent loop: tool-calling + grammar/JSON constrained, max_steps, cancel, whitelist.
4. L0 delante; L1 dicta QuerySeeds en NL conceptual (D17).
5. Intención `needs_level2` sin llamar aún al coder.

**Done:** agent offline con tools reales; seeds + tools visibles en tab AI.

### Fase D — Puente L2 dry-run (sin modelo complejo)  ← estadio intermedio clave

Objetivo: validar el payload hacia L2 **antes** de descargar Coder-7B (~4.5 GB) o configurar API.

Cuando L1 decide `needs_level2` (o `/l2`):

1. L1 ensambla instrucción + mapa rankeado (`map_last.md`) sin bodies.
2. Con `ai.level2.mode = dry_run` (default): auditar mapa en tab AI / `.tuide/ai/map_last.md`.
3. Con `ai.level2.mode = harness`: bootstrap de `.tuide/ai/l2/session.md` (tool guide + instruction + mapa + observations). Un agente Cursor escribe `request.json`; tuide ejecuta `/l2_turn` / `/l2_tool` y acumula observaciones (trim de las más antiguas). Ver `docs/ai/l2-harness-prompt.md` y `tools/l2_harness.sh`.
4. `edit` / coder real quedan para Fase E (`local` | `remote`).

**Done:** dry-runs / harness revisados en casos reales sin peso L2.

### Fase E — Nivel 2 real

1. Backend L2: **Qwen2.5-Coder-7B-Instruct** Q4 (o Coder-1.5B) **y/o** API remota (D3, D7, D18).
2. `ai.level2.mode = local | remote` (dry_run / harness siguen como debug).
3. Parser Search/Replace (D12) → apply + journal `Level2_AI` — **implementado**.
4. Autofix acotado: compile post-edit + ≤3 reintentos + rollback + feedback old/new — **implementado**.
5. Loop autónomo + streaming de fases al tab AI — **implementado** (`run_level2_autonomous`, `/l2_run`). Ver `docs/ai/l2-autonomous.md`.
6. Myers opcional (worker) — pendiente.

**Done (código):** cerebros `LocalL2Brain` / `RemoteL2Brain`, paquete `ai-l2`, config `ai.level2.*`. Validar en máquina con RAM/GPU o API remote.

### Fase F — RAG (roadmap, no bloquea E)

`RagProvider` rellena el mismo `ContextPack` que ya consume el dry-run y L2. Sin cambiar el contrato del puente.

### Fase G — UX producto

Accept/reject hunks (opcional), theme gutter, i18n, atajo foco tab AI, docs usuario.

### Resumen de dependencias

| Fase | Depende de | Entrega principal |
|---|---|---|
| A Bases | — | Tab AI, tools, tasks, journal, ContextPack vacío-de-RAG |
| B L0 | A | Router sin LLM |
| C L1 | A+B | Agent 1.5B (upgrade Phi-4/7B) + tools |
| D Dry-run | C | Preview del envío a L2 en consola |
| E L2 | D (validado) | Coder-7B Apache / API + Search/Replace |
| F RAG | E (o en paralelo tras D si el pack está estable) | Embeddings |
| G UX | E+ | Pulido |

---

## 10. Qué NO hacer

1. Reescribir LSP, Tree-sitter, rg, Git, wake UI.
2. Dual-buffer paralelo al `EditorBuffer`.
3. Git blame/polling para atribución.
4. RAG obligatorio en v1.
5. Pesos **solo no-comercial** (Qwen Research 3B) o runtimes **GPLv3** que contagien el binario; Qwen 0.5B como único cerebro de tools.
6. Devolver archivos enteros desde L2.
7. Myers en el hilo UI en cada tecla.
8. Introducir `.tuide.json` en la raíz.
9. Confundir `src/build/` (compile_commands) con el Task Runner.
10. Panel de chat lateral o flotante aparte del host de tabs de consola (D15: tab **AI** abajo).
11. Volcar archivos enteros o diffs enormes en el transcript (el código va al editor).
12. Enviar keystrokes del tab AI a la PTY del Terminal.
13. Python runtime para IA.
14. Saltar a L2/modelo coder sin pasar por **dry-run** del payload en el tab AI (D16).
15. Implementar L1 antes de tener Bases + L0 operativos.
16. Adjuntar en Releases pesos que **prohíban redistribución** o sean **solo no-comercial** (p.ej. Qwen Research).

---

## 11. Decisiones Q-A … Q-F (cerradas)

| Id | Pregunta | Resolución |
|---|---|---|
| Q-A | Modelo L1 | Revisado: default **1.5B** Instruct; Phi-4-mini/7B como upgrade (**D9**) |
| Q-B | Shell del agent | **Whitelist** de comandos; compile/launch por defecto; fuera = deny / “añadir a whitelist” (**D10**) |
| Q-C | Color gutter | AI = **azul**; humano = verde (**D11**) |
| Q-D | Formato S/R | **Aider-text** adaptado a C++; JSON solo si hace falta (**D12**) |
| Q-E | Persistencia | **Sidecar** en `.tuide/` (**D13**) |
| Q-F | ¿Todo por L0? | **Sí** — más conveniente (latencia, CPU, determinismo); slash = atajos L0 (**D14**) |

### Dudas menores restantes (no bloquean el diseño)

1. ¿Selector de tier L1 en UI (1.5B / Phi-4 / 7B) desde el primer día?
2. ¿Tier L2 ligero (Coder-1.5B) además del Coder-7B default?
3. ¿El azul AI del gutter reutiliza un token del theme o `theme.ai_gutter`?

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
- [x] Roadmap estricto Bases → L0 → L1 → L2-dry-run → L2 (**D16**)
- [x] ContextPack sin RAG = QuerySeed (L1) + rg/TS/LSP (**D17**)
- [x] Licencias redistribuibles (D18); L1 default **1.5B** + upgrade Phi-4; L2 **Coder-7B**; no Qwen Research 3B
- [ ] (Opcional) Checklist de issues por fase cuando se abra implementación
- [ ] Sin código de producto en esta rama hasta que se pida explícitamente
