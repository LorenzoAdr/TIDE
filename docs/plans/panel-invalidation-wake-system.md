# Plan: dirty por panel FTXUI + busy strip ANSI

**Estado:** pendiente de implementación  
**Fecha:** 2026-08-07 (rev. separación dirty paneles vs strip ANSI)  
**Rama de diseño:** `cursor/panel-invalidation-plan-39c9`  
**Alcance:** diseño normativo; este documento no implementa el sistema.

## 0. Dos sistemas (no mezclar)

Este plan une **dos piezas distintas**. Deben permanecer separadas:

| Sistema | Qué es | Para qué | Entra en dirty / `UI_WAKE`? |
|---------|--------|----------|------------------------------|
| **A. Dirty por panel FTXUI** | Cache de `Element` + `UiInvalidation` | Scroll editor, open/jump, TS→outline, terminal, etc.: no rebuild de paneles que no cambiaron | **Sí** — wake FTXUI, pero rebuild solo de paneles dirty |
| **B. Busy strip ANSI** | Franja fija indicador+label en la status bar | Liveness (`indexing`, `git push`, `%`… ) sin tumbar la UI | **No** — fuera del dirty; se pinta con escapes ANSI |

**Desaparece del plan:** rate-limit / wake a 2 Hz / cache de “mensajes de status” como panel FTXUI. La franja **no** es un `UiPanelId` ni pasa por `set_status_text` + dirty.

Chrome FTXUI de la fila inferior (foco + botones): sigue en FTXUI. Solo el **hueco del mensaje** es el strip ANSI.

```text
[Editor|Helix…]  ⠋   indexing…………    [Index][Launch][Debug]…
└── FTXUI ──────┘  └── ANSI strip ──┘  └── FTXUI toolbar ────┘
     (dirty A)         (sistema B)           (dirty A / chrome)
```

---

## 1. Contexto y problema

FTXUI v6.1.9 (`ScreenInteractive::Draw`) siempre:

1. Ejecuta `component->Render()` sobre el documento completo.
2. Rellena el framebuffer.
3. Vuelca la pantalla entera con `ToString()`.

No hay API de paint regional. Un `UI_WAKE` → `PostEvent(Custom)` implica un `Draw` global.

Hoy:

- `UiPanelRenderCache` existe (`FileTree`, `EditorCenter`, `RightSidebar`, `Console`), pero `MakeCachedPanelRender` solo envuelve **RightSidebar**.
- Muchos call sites usan `UI_WAKE("wake")` o `invalidate_editor_view()` (Editor + RightSidebar siempre).
- El mensaje de status es `text | flex` dentro del root; actualizarlo con wakes frecuentes es caro.
- `EditorGridNode` abarata el paint del editor **dentro** de un Draw; no evita el wake global ni el rebuild de otros paneles.

### Fuera de alcance (explícito)

- Fork / parche de FTXUI para dirty-rows o diff de frame.
- Reservar `H−1` filas (FTXUI dueño de toda la terminal menos la última).
- Sustituir la status bar entera (botones/popover) por solo texto.
- Rate-limit de mensajes de status vía dirty/wake FTXUI (sustituido por strip ANSI).
- Meter el busy strip dentro de `UiPanelRenderCache`.

**En alcance sistema B:** ANSI sobre el rectángulo fijo del strip (indicador W + label N); post-`Draw` reassert. Microcorte al pisar FTXUI: aceptable.

---

## 2. Objetivos

### Sistema A — dirty paneles

1. **Event-driven:** sin cambio visible → 0 wakes.
2. **Dirty por panel:** solo se regenera el `Element` de paneles marcados dirty (clave p. ej. en scroll del editor: no reconstruir FileTree/Console/Sidebar).
3. **Invalidación declarativa:** cada tipo de evento → set fijo de paneles.
4. **Rate-limit genérico opcional por panel** (infra lista; v1 todos `nullopt`; futuro p. ej. Console). **No** aplica al busy strip.

### Sistema B — busy strip

5. Mensaje de actividad = franja fija Braille|`%` + label, **solo ANSI**, fuera de dirty.
6. Batería v1 de actividades (§15); descargas/`%` y core en otras ramas.

---

## 3. Principios

| Principio | Detalle |
|-----------|---------|
| Wake ≠ rebuild | Un wake sigue siendo `Draw` global FTXUI; dirty evita **rebuild** de Elements no afectados. |
| Strip ≠ panel | El busy strip no se marca dirty ni despierta la UI por sí solo. |
| Un archivo de política A | `ui_invalidation_policy.hpp` (evolución de `ui_wake_policy.hpp`). |
| Sin `"wake"` genérico | Paths migrados usan `invalidate(UiInvalidation, tag)`. |
| Evento A no listado | No despierta (o se añade fila antes). |
| Actividad B | `set_busy_*` / `clear_busy` → ANSI (+ reassert); 0 `UI_WAKE` por tick/label del strip. |

---

## 4. Modelo de paneles (solo sistema A)

`UiPanelId` — **sin** StatusBar como panel de mensajes:

| Id | Contenido |
|----|-----------|
| `FileTree` | explorador + badges git |
| `EditorCenter` | tabs + buffer + overlays del editor |
| `RightSidebar` | outline / search / watches / stack debug |
| `Console` | terminal + problems + git tab inferior |

La fila de status en el root FTXUI compone: `foco` + **placeholder** del strip (espacios de ancho W+1+N) + `botones`. El placeholder existe para reservar geometría / hit-test de botones; su contenido visible lo pinta ANSI.

Invalidación global (resize, tema vía `colors_revision`, layout/modo): `mark_all_dirty()` / `ChromeAll`.

### Sets compuestos

```text
EditorOnly          = { EditorCenter }
EditorOutline       = { EditorCenter, RightSidebar }
OpenOrJumpFile      = { EditorCenter, RightSidebar, FileTree }
DebugStopSet        = { EditorCenter, RightSidebar } (+ Console si muestra log)
ChromeAll           = { FileTree, EditorCenter, RightSidebar, Console }
```

Hover/press de botones de status: wake FTXUI inmediato del chrome de la fila (barato); **no** pasa por rate-limit de “status text” (ya no existe).

---

## 5. Infraestructura sistema A

### 5.1 Rate-limit genérico (opcional; no para el strip)

**Nuevo:** `src/ui/ui_panel_wake.hpp` (+ `.cpp` si aplica).

```text
struct PanelWakePolicy {
  std::optional<int> max_hz;  // nullopt = sin techo
};
```

| Panel | `max_hz` v1 |
|-------|-------------|
| FileTree / EditorCenter / RightSidebar / Console | `nullopt` |

Infra lista para techo futuro en Console; **ningún techo ligado a mensajes de status**.

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
  LspSemanticTokens,
  VisualHighlight,
  FindMatches,
  StatusChrome,            // hover/press botones status (FTXUI), inmediato
  TerminalOutput,
  DebugStopped,
  DebugContinued,
  DebugSessionReady,
  FileTreeStructure,
  IndexerFsChange,         // dirty FileTree; progreso visible → busy strip B, no dirty status
  ThemeOrResize,
  AppModeChanged,
  LayoutChromeChanged,
};

void invalidate(MainLayoutState*, UiInvalidation kind, std::string_view tag = {});
```

**No hay** `StatusText` / `StatusTextUrgent` / `IndexerProgressText` como invalidación FTXUI. El progreso de index se refleja con `set_busy_spinner(Indexing)` (sistema B).

Algoritmo `invalidate(kind)`:

1. `spec = policy[kind]` → `mark_dirty` paneles.
2. Wake según rate-limit del panel (v1: siempre inmediato si hay dirty).
3. Emit vía `UiEventDispatcher` (único `PostEvent(Custom)`).

---

## 6. Tabla normativa evento → paneles (sistema A)

Lo no listado **no** se marca dirty. El busy strip **nunca** aparece en esta tabla.

### 6.1 Archivos y navegación

| `UiInvalidation` | Dirty | No tocar | Notas |
|------------------|-------|----------|-------|
| `OpenFile` | `OpenOrJumpFile` | Console | Reveal + outline path + buffer. Actividad outline → strip B. |
| `JumpCrossFile` | igual que open | Console | |
| `JumpSameFile` | `EditorOnly`; + RS si outline resalta símbolo | FileTree, Console | |
| Nav programada | preferible no paint; wake en **Complete** | — | |
| Cerrar / switch tab | Ed + RS (+ FT si reveal) | Console | |
| Guardar | Ed si dirty-dot; feedback → strip B si se quiere | | |

### 6.2 Tree-sitter / outline

| `UiInvalidation` | Dirty | No tocar | Notas |
|------------------|-------|----------|-------|
| `TreeSitterActiveFile` | Ed + RS | FT, Console | Outline + color. Strip B: `OutlinePending` hasta ready. |
| `TreeSitterInactiveFile` | ninguno | todo | |
| Typing burst | encolar dirty `EditorOutline`, diferir wake | — | |
| `LspDocumentSymbols` | ninguno (v1) | — | |

### 6.3 LSP / overlays

| `UiInvalidation` | Dirty | No tocar |
|------------------|-------|----------|
| `LspCompletion` | `EditorOnly` | RS, FT, Console |
| `LspHover` | `EditorOnly` | idem |
| `LspDiagnostics` | Ed + Console si tab Problems visible | FT; RS no salvo problems en RS |
| Contadores en status antiguos | — | Van al strip B solo si se decide; v1 no |
| `VisualHighlight` / `FindMatches` | `EditorOnly` | |

### 6.4 File tree / índice

| `UiInvalidation` | Dirty | No tocar |
|------------------|-------|----------|
| `FileTreeStructure` | `FileTree` | resto |
| `IndexerFsChange` | `FileTree` | resto; **progreso** → `BusyActivity::Indexing` (B) |
| Indexer silent modify | nada | |

### 6.5 Terminal / debug / git

| `UiInvalidation` | Dirty | No tocar | Rate |
|------------------|-------|----------|------|
| `TerminalOutput` | `Console` | resto | v1 ilimitado |
| `DebugStopped` / … | `DebugStopSet` (+ Console si output) | FT salvo open frame | inmediato |
| Git UI interactiva | Console y/o FT | | push/pull también activan strip B |
| GitIndexerUpdated | no wake | | |

### 6.6 Chrome status (FTXUI, no strip)

| `UiInvalidation` | Dirty | Notas |
|------------------|-------|-------|
| `StatusChrome` | (rebuild barato del root chrome / sin panel cache de mensaje) | Hover botones; inmediato |

### 6.7 Chrome global

| `UiInvalidation` | Dirty |
|------------------|-------|
| `ThemeOrResize` | `ChromeAll` (+ recalcular geometría del strip B) |
| `LayoutChromeChanged` | paneles afectados |
| `AppModeChanged` | `ChromeAll` o Ed+RS (+Console) |

---

## 7. Anti-patrones → remedio

| Anti-patrón | Remedio |
|-------------|---------|
| `invalidate_editor_view` siempre marca RS | `UiInvalidation` fino |
| `UI_WAKE("wake")` | `invalidate(...)` |
| Wake FTXUI para actualizar `%` / “indexing…” | `set_busy_*` ANSI |
| Meter StatusBar en dirty cache por mensajes | Prohibido; strip es B |
| Paint-time auto-dirty “por si acaso” | Writers declaran A |
| Progress index sin feedback | Strip B `Indexing`, no dirty status |

---

## 8. Integración dispatcher (sistema A)

- Tags estables (`tree_sitter.ready`, `editor.open`, …).
- Coalescing `Custom` se mantiene.
- `emit_terminal` / `emit_debug` → `invalidate(...)`.
- Traza: paneles dirty; **no** contadores de “status_wakes” por mensaje (el strip no wakea).

---

## 9. Fases de implementación

| Fase | Trabajo | Sistema |
|------|---------|---------|
| **1** | `UiInvalidation` + `invalidate()` + policies; rate-limit genérico opcional (todo `nullopt`) | A |
| **2** | Cablear `MakeCachedPanelRender` en FT / EditorCenter / Console (+ RS ya) | A |
| **3** | Migrar OpenFile, Jump*, TreeSitterActiveFile, TerminalOutput, DebugStopped | A |
| **4** | Romper `invalidate_editor_view`; completion/hover/diagnostics/VH/find | A |
| **5** | Busy strip UI: quitar app name; placeholder + ANSI; API `set_busy_*`; post-Draw reassert | B |
| **5b** | Hooks batería v1 (§15) | B |
| **6** | Tests golden dirty matrix (A) + tests strip sin wake (B) | A+B |
| **7** | Docs / checklist anti-`"wake"` | — |
| **8** *(futuro)* | `max_hz` en Console si hace falta | A |

Orden sugerido: **1 → 2 → 3 → 6(A) → 4 → 5 → 5b → 6(B) → 7**.

El dirty A aporta desde ya al scroll del editor (no rebuild de vecinos). El strip B es independiente y puede ir en paralelo tras tener geometría de status estable.

---

## 10. Tests y criterios de éxito

### Sistema A

- Scroll/tecla editor: dirty solo `EditorCenter` (salvo policy que diga más); generations de FT/Console/RS quietas si no aplican.
- `OpenFile`: Ed+RS+FT; Console intacta.
- `TreeSitterActiveFile`: Ed+RS.
- `LspCompletion`: no dirty RS.
- `max_hz = nullopt`: cada invalidate despierta.

### Sistema B

- `set_busy_spinner` / cambio de `%` → **0** `UI_WAKE` / **0** dirty de paneles.
- Mismo ancho W en Braille y `%`.
- Tras un Draw FTXUI, reassert restaura strip (microcorte OK).
- Batería v1 conectada (indexing, hierarchy, refs, search, git push/pull, outline).

### Regresiones

- Botones status / popover Layout.
- Helix en foco.
- Geometría strip tras resize.
- Reveal file tree; debug ►; typing burst outline.

---

## 11. Archivos tocados (previsión)

| Área | Paths |
|------|-------|
| A Policy / wake | `ui_invalidation_policy.hpp`, `ui_panel_wake.hpp`, `ui_wake.hpp`, `ui_event_dispatcher.*` |
| A Cache | `ui_panel_render_cache.hpp`, `main_layout.cpp` / `.hpp` |
| B Strip | nuevo `busy_strip.*` (o similar), `main_layout` placeholder, hooks en indexer / call_hierarchy / search / git / outline |
| Call sites A | editor, outline, file_tree, application (TS/LSP), terminal/debug channels |
| Docs | este archivo; `docs/README.md`; `architecture.md` |

---

## 12. Referencias de código actual

| Pieza | Ubicación |
|-------|-----------|
| Status render | `src/ui/main_layout.cpp` |
| Cache paneles | `src/util/ui_panel_render_cache.hpp` |
| Solo RS cacheado | `MakeCachedPanelRender` en `main_layout.cpp` |
| Wake policy async | `src/ui/ui_wake_policy.hpp` |
| Dispatcher | `src/ui/ui_event_dispatcher.cpp` |
| Outline ← TS | `src/ui/outline_panel.cpp` |
| Editor pixel grid | `src/ui/editor_grid_node.hpp` |
| Guard PostEvent | `tools/check_ui_wake.sh` |

---

## 13. Resumen ejecutivo

**A:** invalidación declarativa + cache de Elements por panel FTXUI → menos rebuild en wakes legítimos (scroll, open, TS, etc.).  
**B:** busy strip (Braille|`%` + label) **fuera de dirty**, solo ANSI; sin wake a tasa fija por mensajes.  
Chrome status = foco + botones FTXUI; sin app name. Sin paint parcial de FTXUI.

---

## 14. Busy strip (sistema B) — detalle

### Producto

Sustituye el `status_msg` flexible. No sustituye la barra entera.

- Quitar `status.app_name`.
- Mantener foco + botones.
- Indicador: Braille (no cuantizable) o `%` (cuantizable), **mismo ancho W**.
- Label truncado ancho N.
- Microcorte si FTXUI pisa: aceptable; reassert post-Draw.

### Layout

| Zona | Ancho | Contenido |
|------|-------|-----------|
| Indicador | W (p. ej. 4) | Braille padded o `%` o espacios |
| Separador | 1 | espacio |
| Label | N fijo | verbo truncado + padding |

Geometría constante al cambiar de modo. Coordenadas ANSI fijas respecto al placeholder del root (actualizar en resize).

### Paint (todo el strip por ANSI)

1. FTXUI reserva placeholder de `W+1+N` espacios (no contenido semántico).
2. `set_busy_*` / ticker escriben **indicador y label** por ANSI (sin `UI_WAKE`).
3. Tras cada `Draw`, reassert del strip completo (o al menos indicador + label actuales).
4. Idle: clear ANSI / espacios; parar ticker.

### API

```text
enum class BusyIndicatorKind { None, Spinner, Percent };

void set_busy_spinner(MainLayoutState*, string_view label);
void set_busy_percent(MainLayoutState*, int percent, string_view label);
void clear_busy(MainLayoutState*);
```

Ninguna de estas funciones hace `invalidate` / `UI_WAKE` por el contenido del strip.

### Relación con A

| Señal | Mecanismo |
|-------|-----------|
| Actividad busy / `%` / label | Solo B (ANSI) |
| Hover botones status | A: `StatusChrome` |
| Open file + outline tardío | A: `OpenFile` / `TreeSitterActiveFile` **y** B: `OutlinePending` |

---

## 15. Batería de actividades (v1, sistema B)

Criterio: esperas ciegas/largas sin modal que ya monopolice. Sin completion/diagnostics. Sin launch DAP. Descargas/`%` y core → otras ramas.

### Incluir

| Activity | Indicador | Label orientativo | Hooks (aprox.) |
|----------|-----------|-------------------|----------------|
| `Indexing` | Spinner | indexing | `start_scan` / `reindex_project` → `!scanning_` |
| `CallHierarchy` | Spinner | call hierarchy | `open_call_hierarchy_view` / expand → done |
| `FindReferences` | Spinner | references | `open_references_view` → done |
| `ProjectSearch` | Spinner | searching | `WorkspaceSearchRunner` running → idle |
| `GitPush` | Spinner | git push | `git->push` → callback |
| `GitPull` | Spinner | git pull | `git->pull` → callback |
| `OutlinePending` | Spinner | esperando outline | open/switch + `!document_symbols_ready` → ready |

Prioridad: la más específica gana; stack LIFO al completar.

### Excluir

Completion, diagnostics, hover, DAP launch modal, stage/commit git cortos, goto-def típico.

### Aplazar

| Momento | Indicador | Nota |
|---------|-----------|------|
| Descarga paquetes | Percent | Otra rama |
| Abrir core | Spinner | Más adelante |
| Format / rename | Spinner | Oleada 2 |
| `compile_commands` gen | Spinner | Oleada 2 |

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
};
```

---

## 16. Aclaraciones abiertas (mínimas)

1. **Geometría del placeholder:** el root FTXUI debe exponer (tras layout) la `Box`/columna base del strip para ANSI; definir un `reflect`/`Box` solo del hueco.
2. **Stdout vs Draw:** serializar ANSI del strip con el flush de FTXUI (mutex o “solo en post-Draw + ticker cuando no hay frame en curso”).
3. **¿El strip muestra también mensajes one-shot** (save ok / error) o solo “busy”? v1 = solo batería busy; one-shots pueden seguir otro camino breve o reutilizar label sin spinner.
4. **Diagnósticos N errors** que hoy van en `status_msg`:** ¿se eliminan de la fila, van al strip idle, o a otro sitio?** Decisión de producto menor al implementar el layout.
