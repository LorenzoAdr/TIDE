# Plan: dirty por panel + invalidación declarativa + rate-limit de status

**Estado:** pendiente de implementación  
**Fecha:** 2026-08-07 (rev. batería de actividades busy strip v1)  
**Rama de diseño:** `cursor/panel-invalidation-plan-39c9`  
**Alcance:** diseño normativo; este documento no implementa el sistema.

## 1. Contexto y problema

FTXUI v6.1.9 (`ScreenInteractive::Draw`) siempre:

1. Ejecuta `component->Render()` sobre el documento completo.
2. Rellena el framebuffer.
3. Vuelca la pantalla entera con `ToString()`.

No hay API de paint regional. Un `UI_WAKE` → `PostEvent(Custom)` implica un `Draw` global.

Hoy:

- `UiPanelRenderCache` existe (`FileTree`, `EditorCenter`, `RightSidebar`, `Console`), pero `MakeCachedPanelRender` solo envuelve **RightSidebar**.
- La barra de estado se arma en el `Renderer` raíz (`main_layout.cpp`) y **no** es un `UiPanelId`.
- Muchos call sites usan `UI_WAKE("wake")` o `invalidate_editor_view()` (Editor + RightSidebar siempre).
- Un porcentaje en status a alta tasa rompería el modelo de la app (wakes selectivos, coalescing).

`EditorGridNode` **no** evita el wake global: solo abarata el `Render` del editor (blit de filas pre-rasterizadas).

### Fuera de alcance (explícito)

- Fork / parche de FTXUI para dirty-rows o diff de frame.
- Reservar `H−1` filas (FTXUI dueño de toda la terminal menos la última).
- Sustituir la status bar entera (botones/popover) por solo texto.
- Rate-limit en editor o terminal en v1 (la API queda lista).
- Porcentajes **solo vía wake FTXUI a alta tasa** (el strip ANSI sí puede mostrar `%`; ver §14).

**En alcance (ornamental):** ANSI sobre el **slot indicador fijo** del busy strip (Braille o `%`, mismo ancho W; ver §14), no un bypass de fila completa.

## 2. Objetivos

1. **Event-driven:** sin cambio visible → 0 wakes.
2. **Dirty por panel:** solo se regenera el `Element` de paneles marcados dirty.
3. **Invalidación declarativa:** cada tipo de evento mapea a un set fijo de paneles (tabla normativa).
4. **Rate-limit genérico por panel:** en v1 solo status a **2 Hz**; el resto `nullopt` (ilimitado).
5. **Status / busy strip:** el mensaje de estado deja de ser un `text | flex` libre; pasa a franja fija con **slot indicador** (Braille *o* `%`, mismo ancho) + label (ver §14). Cambio de label → dirty/rate-limit; frames del indicador → ANSI sin wake.
6. Dejar el sistema montado para aplicar techos futuros (p. ej. output de terminal).

## 3. Principios

| Principio | Detalle |
|-----------|---------|
| Wake ≠ rebuild | Un wake sigue siendo `Draw` global FTXUI; dirty evita **rebuild** de Elements no afectados. |
| Un solo archivo de política | `ui_invalidation_policy.hpp` (evolución de `ui_wake_policy.hpp`). |
| Sin `"wake"` genérico | Paths migrados usan `invalidate(UiInvalidation, tag)`. |
| Evento no listado | No despierta (o se añade fila a la tabla antes de usarlo). |
| Rate-limit solo donde la policy lo diga | Status texto sí; hover de toolbar status no; editor no. |
| Un wake diferido máximo por panel rate-limited | Sin bucles de `RequestAnimationFrame`. |

## 4. Modelo de paneles

Extender `UiPanelId` en `src/util/ui_panel_render_cache.hpp` (o split a `ui_panel_id.hpp`):

| Id | Contenido |
|----|-----------|
| `FileTree` | explorador + badges git |
| `EditorCenter` | tabs + buffer + overlays del editor |
| `RightSidebar` | outline / search / watches / stack debug |
| `Console` | terminal + problems + git tab inferior |
| `StatusBar` *(nuevo)* | fila inferior |

Invalidación global (resize, tema vía `colors_revision`, cambio estructural de layout/modo): `mark_all_dirty()` / `ChromeAll`.

### Sets compuestos (nombres fijos en política)

```text
EditorOnly          = { EditorCenter }
EditorOutline       = { EditorCenter, RightSidebar }
OpenOrJumpFile      = { EditorCenter, RightSidebar, FileTree }  // reveal selección
DebugStopSet        = { EditorCenter, RightSidebar, StatusBar } (+ Console si muestra log)
StatusOnly          = { StatusBar }   // max_hz=2, solo si texto cambia
ChromeAll           = todos
```

## 5. Infraestructura

### 5.1 Rate-limit genérico

**Nuevo:** `src/ui/ui_panel_wake.hpp` (+ `.cpp` si aplica).

```text
struct PanelWakePolicy {
  std::optional<int> max_hz;  // nullopt = sin techo
};

struct PanelWakeState {
  int64_t last_wake_ms = 0;
  bool wake_pending = false;
  std::string last_committed_text;  // status: último texto aceptado/pintado
  std::string pending_text;         // status: último valor en cola
};
```

Políticas por defecto (v1):

| Panel | `max_hz` |
|-------|----------|
| `StatusBar` | `2` (intervalo mínimo 500 ms) |
| `Console` | `nullopt` (listo para p. ej. 10–15 después) |
| `EditorCenter` / `FileTree` / `RightSidebar` | `nullopt` |

### 5.2 API de invalidación declarativa

```text
enum class UiInvalidation : uint16_t {
  OpenFile,
  JumpSameFile,
  JumpCrossFile,
  TreeSitterActiveFile,
  TreeSitterInactiveFile,  // no wake
  LspCompletion,
  LspHover,
  LspDiagnostics,
  LspDocumentSymbols,      // vestigial: no wake en v1
  LspSemanticTokens,       // no wake si TS manda color
  VisualHighlight,
  FindMatches,
  StatusText,              // rate-limited 2 Hz + dedupe
  StatusChrome,            // hover/press toolbar; inmediato
  StatusTextUrgent,        // force_immediate (errores, save fail)
  TerminalOutput,
  DebugStopped,
  DebugContinued,
  DebugSessionReady,
  FileTreeStructure,
  IndexerFsChange,
  IndexerProgressText,     // StatusText path
  ThemeOrResize,
  AppModeChanged,
  LayoutChromeChanged,
  // …
};

struct UiInvalidationSpec {
  /* bitset */ panels;
  std::optional<int> max_hz;     // override; suele venir del panel
  bool force_immediate = false;
  UiEventKind drain_kind;
  std::string_view default_tag;
};

void invalidate(MainLayoutState*, UiInvalidation kind, std::string_view tag = {});
void set_status_text(MainLayoutState*, std::string message, StatusTarget target);
```

Algoritmo `invalidate(kind)`:

1. Resolver `spec = policy[kind]`.
2. `mark_dirty` de cada panel en `spec.panels`.
3. Pedir wake según rate-limit del spec / paneles afectados.
4. Emitir evento con tag estable (`panel.status`, `tree_sitter.ready`, …) vía `UiEventDispatcher` (único sitio con `PostEvent(Custom)`; respeta `tools/check_ui_wake.sh`).

Algoritmo `set_status_text`:

1. Si `message == last_committed_text` y no hay pending distinto → **return** (0 Hz).
2. Escribir modelo (`workspace_` / `model_` según target).
3. `pending_text = message`; `mark_dirty(StatusBar)`.
4. Si elapsed ≥ 500 ms desde último wake de status → wake ahora; si no → `wake_pending` + **un** wake diferido al deadline.
5. Al disparar: commit `last_committed_text = pending_text`.

Wake diferido: `screen->Post` / one-shot en drain existente. **Prohibido** ticker fijo de status.

### 5.3 Cache de Elements

Cablear `MakeCachedPanelRender` en `MakeMainLayout` para:

1. `FileTree`
2. `EditorCenter` (centro editor apropiado)
3. `Console`
4. `RightSidebar` (ya existe)
5. `StatusBar`: el `Element` de la fila status vía `panel_render_cache.render(StatusBar, …)` dentro del root

El root sigue siendo `vbox({ main, status })`; status solo se **reconstruye** si dirty (o `colors_revision`).

Matiz status chrome vs mensaje (v1):

- Rate-limit **solo** en `StatusText` / `set_status_text`.
- Hover/press de botones → `StatusChrome` → dirty StatusBar **sin** techo.
- (Opcional v2: partir StatusMessage vs StatusToolbar en dos leaves.)

## 6. Tabla normativa evento → paneles

Regla: lo no listado **no** se marca dirty.

### 6.1 Archivos y navegación

| `UiInvalidation` | Dirty | No tocar | Notas |
|------------------|-------|----------|-------|
| `OpenFile` | `OpenOrJumpFile` + `StatusBar` si hay mensaje | Console | Reveal tree + outline path + buffer. |
| `JumpCrossFile` | igual que open | Console | Path cambió. |
| `JumpSameFile` | `EditorOnly`; + `RightSidebar` solo si outline debe resaltar símbolo | FileTree, Console | |
| Nav programada | preferible **no paint**; un solo wake en **Complete** | — | Evitar doble scheduled+complete. |
| Cerrar / switch tab | Ed + RS (+ FT si reveal) | Console | |
| Guardar | Status (+ Ed si dirty-dot en tab) | outline/terminal si no cambian | |

### 6.2 Tree-sitter / outline

| `UiInvalidation` | Dirty | No tocar | Notas |
|------------------|-------|----------|-------|
| `TreeSitterActiveFile` | `EditorCenter` + `RightSidebar` | FT, Console, Status | **Obligatorio RS**: outline se calcula ahí. |
| `TreeSitterInactiveFile` | ninguno | todo | Solo cache interna TS. |
| Typing burst (&lt;250 ms) | encolar dirty `EditorOutline`, diferir wake | — | Outline puede ir 1 frame atrasado a propósito. |
| `LspDocumentSymbols` | **ninguno** (v1; outline = TS) | — | Vestigial. |

### 6.3 LSP / overlays editor

| `UiInvalidation` | Dirty | No tocar |
|------------------|-------|----------|
| `LspCompletion` | `EditorOnly` | RS, FT, Console, Status |
| `LspHover` | `EditorOnly` | idem |
| `LspDiagnostics` | Ed + Status (contadores) + Console **solo si** tab Problems visible | FT; RS no salvo problems en RS |
| `LspSemanticTokens` | Ed si se usan; si TS manda color → no wake | |
| `VisualHighlight` | `EditorOnly` (**dejar** de ensuciar RS siempre) | |
| `FindMatches` | `EditorOnly` | reason dedicado, no `"wake"` |

### 6.4 File tree / índice

| `UiInvalidation` | Dirty | No tocar |
|------------------|-------|----------|
| `FileTreeStructure` (expand/scroll/hover/select sin open) | `FileTree` | resto |
| `IndexerFsChange` (`wake_ui=true`) | `FileTree` | resto |
| `IndexerProgressText` | `StatusOnly` (2 Hz, dedupe) | no FT salvo badges |
| Indexer silent modify | nada | |

### 6.5 Terminal / debug / git

| `UiInvalidation` | Dirty | No tocar | Rate |
|------------------|-------|----------|------|
| `TerminalOutput` | `Console` | resto | v1 ilimitado; API lista para `max_hz` |
| `DebugStopped` / `Continued` / `SessionReady` | `DebugStopSet` (+ Console si output) | FT salvo open de frame | inmediato |
| Variables/stack post-stop | RS (+ Ed si ►/línea) | | acoplado al wake de stop |
| Git UI interactiva | Console (tab git) y/o FT (badges) | | |
| GitIndexerUpdated async | **no wake** (como hoy) | | dirty FT si revision cambia en próximo paint con otro wake |

### 6.6 Status / busy strip

| `UiInvalidation` | Dirty | Rate |
|------------------|-------|------|
| `StatusText` (cambio de **label** del strip) | `StatusOnly` | max 2 Hz, dedupe, coalesce |
| label igual | **0** wakes | |
| Tick del indicador (Braille o `%`) | **ninguno** | ANSI al slot fijo; sin `UI_WAKE` |
| `StatusChrome` (hover/press botones) | StatusBar | inmediato |
| `StatusTextUrgent` | StatusBar | `force_immediate` |

Chrome de la fila (producto): **foco + busy strip + botones**. Sin `status.app_name`.  
Gate actual `if (!editor_focus)` sobre el mensaje: reevaluar al montar el strip (el busy de index/download debe poder verse también al editar).

### 6.7 Chrome global

| `UiInvalidation` | Dirty |
|------------------|-------|
| `ThemeOrResize` | `ChromeAll` |
| `LayoutChromeChanged` | paneles afectados + vecinos |
| `AppModeChanged` | `ChromeAll` o Ed+RS+Status (+Console si layout debug) |

## 7. Anti-patrones → remedio

| Anti-patrón actual | Remedio |
|--------------------|---------|
| `invalidate_editor_view` siempre marca RS | Sustituir por `UiInvalidation` fino |
| `UI_WAKE("wake")` / `"app"` genérico | Prohibido en paths migrados |
| Paint-time auto-dirty “por si acaso” | Writers declaran policy; paint solo fallback (resize, colors) |
| Doble wake scheduled + complete | Un wake en complete |
| TS ready sin outline | Cubierto por `TreeSitterActiveFile` |
| Progress index sin wake (`WorkspaceIndexUpdated` silencioso) | `IndexerProgressText` / `set_status_text` |
| `FindMatchesUpdated` definido pero no usado | Migrar find a `FindMatches` |
| `UiWakeReason::TerminalOutput` / `Debug*` bypassed por channels | Channels llaman `invalidate(...)` |

## 8. Integración con dispatcher y traza

- Tags estables por invalidación (`tree_sitter.ready`, `panel.status`, `editor.open`, …).
- Coalescing global de `Custom` se mantiene; el rate-limit es **antes**, por panel.
- `emit_terminal` / `emit_debug`: migrar a `invalidate(TerminalOutput|DebugStopped|…)`.
- Opcional: campo en `UiEvent` con bitset de paneles (debug/perf).
- Contadores: `status_wakes_suppressed` / `status_wakes_emitted` en trace o `UiPerfMonitor`.

## 9. Fases de implementación

| Fase | Trabajo | Entregable |
|------|---------|------------|
| **1** | `UiInvalidation` + specs + `invalidate()` + `set_status_text` + `PanelWakePolicy` (Status 2 Hz) | API + tests unitarios del rate-limit/dedupe |
| **2** | Cablear `MakeCachedPanelRender` en FT/Ed/Console + cache Element StatusBar | Frames status no rebuild de otros paneles |
| **3** | Migrar invalidaciones gordas: OpenFile, Jump*, TreeSitterActiveFile, StatusText, TerminalOutput, DebugStopped | Call sites principales sin `"wake"` |
| **4** | Romper `invalidate_editor_view`; migrar completion/hover/diagnostics/VH/find | Invalidación fina |
| **5** | Migrar `Application::set_status` / `set_workspace_status` → API del busy strip + barrido `status_message =` | Una sola vía de mensaje |
| **5b** | Busy strip UI + batería v1 (§15): Indexing, CallHierarchy, FindReferences, ProjectSearch, GitPush/Pull, OutlinePending | Liveness sin wake; hooks begin/end |
| **6** | Tests golden: cada `UiInvalidation` → set exacto de paneles dirty | Tabla ejecutable |
| **7** | Docs: este plan + sección en `architecture.md`; checklist PR anti-`"wake"` | Normativa visible |
| **8** *(futuro)* | `Console` con `max_hz` opcional en policy table | Sin rediseñar pipeline |

Orden sugerido: 1 → 2 → 3 → 6 (smoke) → 4 → 5 → **5b** → 7 → 8.

## 10. Tests y criterios de éxito

### Tests

- Mismo texto status dos veces → 0 wakes.
- 10 cambios de status en 100 ms → ≤1 wake inmediato + ≤1 diferido; último texto gana.
- Tras ≥500 ms + cambio → otro wake.
- `max_hz = nullopt` → cada invalidate despierta (reloj mock).
- Golden matrix: `OpenFile` dirty Ed+RS+FT, Console generation intacta.
- `TreeSitterActiveFile` dirty Ed+RS; outline sale de loading.
- `JumpSameFile` no dirty FileTree.
- `LspCompletion` no dirty RightSidebar.

### Éxito de producto

- Abrir archivo no rebuild de terminal (generation Console igual).
- TS ready del activo despierta outline sin tocar Console/Status.
- Cambio de label del busy strip ≤ 2 wakes/s; ticks de indicador (Braille/`%`) **0** wakes.
- Mismo ancho de layout en modo spinner y modo porcentaje (sin saltos de columna).
- Input (teclas, hover toolbar status) sin retraso artificial de 500 ms.
- Añadir techo a Console = cambiar un `optional<int>` en la tabla de policies.
- Status bar sin app name; strip visible con actividad larga sin sensación de UI congelada.

### Regresiones a vigilar

- Hover/click botones status, popover Layout.
- Helix mode en focus label.
- Truncate/i18n del label; slot indicador estable tras resize; alinear `%` (p. ej. `100%`) en el mismo ancho que el Braille.
- Reveal en file tree al abrir.
- Debug ► / active line tras stop.
- Typing burst: outline diferido pero eventualmente consistente.

## 11. Archivos tocados (previsión)

| Área | Paths |
|------|-------|
| Policy / wake | `src/ui/ui_wake_policy.hpp` → `ui_invalidation_policy.hpp`, `ui_panel_wake.hpp`, `ui_wake.hpp`, `ui_event_dispatcher.*`, `ui_event_types.hpp` |
| Cache | `src/util/ui_panel_render_cache.hpp`, `src/ui/main_layout.cpp` / `.hpp` |
| Status | `src/app/application.cpp` (`set_status*`), writers de `status_message` |
| Call sites | `editor_panel.cpp`, `outline_panel.cpp`, `file_tree_panel.cpp`, `application.cpp` (TS/LSP/indexer), `terminal_ui_channel.*`, `debug_ui_channel.*`, etc. |
| Tests | `tests/` nuevos o extendidos para policy + panel wake |
| Docs | este archivo; enlace desde `docs/README.md` y `architecture.md` |

## 12. Referencias de código actual

| Pieza | Ubicación |
|-------|-----------|
| Status render | `src/ui/main_layout.cpp` (~status `hbox` / `theme::StatusBar`) |
| Cache paneles | `src/util/ui_panel_render_cache.hpp` |
| Solo RS cacheado | `MakeCachedPanelRender` en `main_layout.cpp` |
| Wake policy async | `src/ui/ui_wake_policy.hpp` |
| Dispatcher | `src/ui/ui_event_dispatcher.cpp` |
| Outline ← TS | `src/ui/outline_panel.cpp` (`refresh_outline_symbols`) |
| Editor pixel grid | `src/ui/editor_grid_node.hpp` |
| Guard PostEvent | `tools/check_ui_wake.sh` |

## 13. Resumen ejecutivo

Sistema event-driven con **invalidación declarativa por tipo de evento**, **cache de Elements en todos los paneles** (incluido status), y **rate-limit genérico** aplicado en v1 al **label** del status (2 Hz, dedupe, coalesce). El mensaje de la barra pasa a un **busy strip** (slot indicador fijo Braille *o* `%` + label) actualizable barato por ANSI; el chrome (foco + botones) se mantiene y se elimina el app name. No intenta paint parcial de FTXUI; reduce rebuild y wakes ruidosos, con matriz explícita de eventos cruzados (open, jump, Tree-sitter → outline).

## 14. Busy strip (reemplazo del mensaje de status)

### Producto

Sustituir el `text(status_msg) | flex` actual por una franja de actividad. **No** se sustituye la barra entera.

```text
[Editor|Helix…]  ⠋   indexing…………    [Index][Launch][Debug][Layout]…
[Editor|Helix…]  47% downloading………  [Index][Launch][Debug][Layout]…
└── foco FTXUI ──┘  └── busy strip ──┘  └── toolbar FTXUI (igual) ──┘
```

- **Quitar** `status.app_name` (no aporta en la fila).
- **Mantener** foco (y Helix si aplica) + botones / popover Layout.
- **Dos modos de indicador**, mismo layout:
  - **No cuantizable** (index, parse, save…): spinner Braille.
  - **Cuantizable** (descargas, etc.): porcentaje.
- Un microcorte si FTXUI pisa el strip es aceptable; el post-hook / tick ANSI restaura el valor.

### Layout del strip (ancho constante)

Regla clave: **el slot del indicador tiene el mismo ancho fijo en ambos modos**, para no mover el label ni complicar coordenadas ANSI.

| Zona | Ancho | Contenido |
|------|-------|-----------|
| Indicador | **W fijo** (p. ej. 4 celdas) | Braille padded (`⠋···`) **o** `%` alineado (`·47%` / `100%`) **o** espacios si idle |
| Separador | 1 | espacio |
| Label | **N fijo** | verbo/detalle truncado con ellipsis + padding |

- Indicador **antes** del label.
- W debe caber `100%` (mínimo práctico **4**). El Braille se centra o se alinea a la izquierda dentro de esas W celdas (el resto espacios).
- N grande pero fijo (entre foco y toolbar; clamp en resize). i18n generoso.
- Cambiar de spinner ↔ `%` **no** cambia W ni N ni la columna base del strip.
- Truncate solo en el label; nunca invadir el slot indicador.

### Paint

1. El `Element` FTXUI reserva `W + 1 + N` celdas (placeholder estático / espacios + label truncado).
2. Ticker / updates escriben por ANSI **solo el slot indicador** (W celdas) y, si el label cambió vía dirty, el label va por el path FTXUI rate-limited — *o* también se puede reasertar label por ANSI si se quiere 0 wakes al cambiar detalle; v1: label por `StatusText`, indicador por ANSI.
3. Tras cada `Draw`, **reasertar** el contenido actual del slot indicador (Braille frame o `%`).
4. Idle: parar ticker; slot indicador = W espacios; label vacío o idle corto.

Frecuencia del indicador (Braille o `%`): libre vía ANSI; **no** pasa por `UI_WAKE`. El microcorte al `Draw` se tolera.

### API prevista

```text
enum class BusyIndicatorKind { None, Spinner, Percent };

struct BusyStripState {
  BusyIndicatorKind kind = BusyIndicatorKind::None;
  int percent = 0;                 // 0..100 si kind == Percent
  std::string label;               // "indexing", "downloading", …
};

void set_busy_spinner(MainLayoutState*, string_view label);
void set_busy_percent(MainLayoutState*, int percent, string_view label);
void clear_busy(MainLayoutState*);
```

- `set_busy_spinner` / cambio de label → `StatusText` si el label cambia.
- `set_busy_percent`: actualizar `percent` por ANSI en el slot W; si solo cambia el número, **0 dirty**; si cambia el label, `StatusText`.
- Misma geometría siempre; solo cambia qué se pinta en las W celdas.

### Relación con dirty/2 Hz

| Señal | Mecanismo |
|-------|-----------|
| Nuevo label / cambio spinner↔% a nivel de chrome FTXUI | Dirty StatusBar + techo 2 Hz (si el Element debe mostrar label nuevo) |
| Mismo label | 0 wakes |
| Frame Braille o nuevo `%` | ANSI sobre slot W (mismo ancho) |
| Hover botones | `StatusChrome` inmediato |

## 15. Batería de actividades del busy strip (v1)

Criterio: solo operaciones **ciegas o largas** donde el usuario no tiene ya un modal/panel que monopolice la espera. Completions/diagnostics y launch DAP **fuera**. Descargas de paquetes y apertura de core → cuando se implementen en sus ramas.

### Incluir en v1

| Activity id | Indicador | Label (orientativo) | Cuándo empieza / termina (código) | Notas |
|-------------|-----------|---------------------|-----------------------------------|-------|
| `Indexing` | Spinner | `indexing` / `indexando` | `WorkspaceIndexer::start_scan` / `reindex_project` / Index button → fin de `scanning_`; opcionalmente arranque LSP “ciego” como mismo verbo | LSP index es ciego (no hay `$/progress` útil hoy). **No** mostrar “esperando sugerencias” ni “esperando errores”. |
| `CallHierarchy` | Spinner | `call hierarchy` / `jerarquía…` | `open_call_hierarchy_view` / `expand_hierarchy_tree` (`call_hierarchy_view.cpp`) → árbol listo o error | RPC + expansión pueden ser largos; hoy poco feedback intermedio. |
| `FindReferences` | Spinner | `references` / `referencias…` | `open_references_view` → `find_references` done | Idem; aporta. |
| `ProjectSearch` | Spinner | `searching` / `buscando…` | `WorkspaceSearchRunner::start` / `run_search` → `!running()` | El panel ya dice searching; el strip ayuda si el foco está en el editor. Sin `%` (hay `files_scanned`, no total fiable). |
| `GitPush` | Spinner | `git push` | `git->push(...)` (`git_panel.cpp`) → callback | Spinner del panel git es local al tab. |
| `GitPull` | Spinner | `git pull` | `git->pull(...)` → callback | Idem. |
| `OutlinePending` | Spinner | `outline` / `esperando outline` | Tras open/switch de archivo: `refresh_outline_symbols` / `prepare_document` mientras `!document_symbols_ready` → symbols ready + wake `outline.symbols` | Cubrir el hueco hasta que TS rellene el outline. |

Prioridad de visualización si coinciden dos: **la más específica gana** (p. ej. `CallHierarchy` > `Indexing`). Cola simple o “current activity” con stack LIFO al completar.

### Excluir en v1 (explícito)

| Momento | Por qué |
|---------|---------|
| Completion / “esperando sugerencias” | Demasiado rápido; overlay local basta |
| Diagnostics / “esperando errores” | Push async ruidoso; gutter/problems bastan |
| Hover LSP | Corto |
| DAP launch / attach / connecting | Ya hay `debug_launch_modal` |
| Stage/unstage/commit git rutinarios | Cortos; spinner del panel git suficiente |
| Refresh status/diff/log git | Cortos / inline loading |
| Goto definition típico | Suele ser corto; no meter ruido |

### Aplazar (otras ramas / más adelante)

| Momento | Indicador previsto | Nota |
|---------|-------------------|------|
| Descarga de paquetes / bundles | **Percent** (mismo slot W) | Rama de descargas; cablear `set_busy_percent` ahí |
| Abrir / analizar core | Spinner | Cuando se toque ese flujo |
| Format / rename LSP (sync hasta 30s) | Spinner | Candidato fuerte en código (`context_menu.cpp`); no pedido ahora — evaluar en oleada 2 |
| `compile_commands` generation | Spinner | Ya hay notas de status; oleada 2 |
| Indexado archivo grande / tabular | Spinner | Editor ya muestra “Indexing…”; oleada 2 si se quiere eco global |
| clangd `$/progress` real | Percent o spinner | Hoy el transport ACK y descarta progress |

### API alineada a la batería

```text
enum class BusyActivity {
  Idle,
  Indexing,
  CallHierarchy,
  FindReferences,
  ProjectSearch,
  GitPush,
  GitPull,
  OutlinePending,
  // oleada 2 / otras ramas:
  // Downloading, OpeningCore, Formatting, Renaming, …
};
```

Hooks de implementación (begin/end o RAII `BusyScope`):

| Activity | Begin (aprox.) | End (aprox.) |
|----------|----------------|--------------|
| Indexing | `Application::reindex_project` / `WorkspaceIndexer::start_scan` | `scanning_ == false` (+ opcional LSP started si se unifica) |
| CallHierarchy | entrada a `open_call_hierarchy_view` / expand | return tras rellenar árbol |
| FindReferences | `open_references_view` | tras `find_references` |
| ProjectSearch | `run_search` / `runner.start` | poll `!running()` |
| GitPush/Pull | justo antes de `git->push/pull` | callback |
| OutlinePending | path activo cambia y symbols no ready | `document_symbols_ready` / `outline.symbols` |

Labels vía i18n (`busy.indexing`, `busy.call_hierarchy`, …), truncados al ancho N del strip.
