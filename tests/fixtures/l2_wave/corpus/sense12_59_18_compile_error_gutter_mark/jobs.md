### Trabajo 1
consulta: dónde se dibujan las líneas rojas en el margen del editor para marcar errores
keep: M7 M2
leído: diagnostics for editor line, Make Editor Panel, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Cerrado:
leído: diagnostics for editor line, Make Editor Panel, line gutter marker, gutter buffer line at row, line diagnostic marker from map
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de la consola para crear los diagnósticos del editor
keep: M1 M9
leído: sync diagnostic cache, diagnostics for translation unit, apply lsp diagnostics to panel, push active file diagnostics from cache, cached file diagnostics
Cerrado:
encontré el mecanismo: los errores se leen desde el LSP vía symbols diagnostics for file llamado por cached file diagnostics y sync diagnostic cache y se almacenan en panel cached file diag luego push active file diagnostics from cache lee esa caché para poblar panel diagnostics by line y pintarlos en el editor.
Abierto:
- diagnostics for file

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para generar diagnósticos
keep: M1 M3
leído: Make Diagnostics Panel
Cerrado:
leído: Make Diagnostics Panel
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, Make Editor Panel, line gutter marker, gutter buffer line at row
    extra: Make Main Layout, ensure buffer

T2
    visto: sync diagnostic cache, diagnostics for translation unit, apply lsp diagnostics to panel, push active file diagnostics from cache
    extra: ISymbol Provider, snapshot

T3
    visto: Make Diagnostics Panel
    extra: Make Console Panel, Make Main Layout

entre abiertas:
  T1=>T2  Make Editor Panel → sync diagnostic cache
  T1=>T3  sin camino
  T2=>T3  Make Diagnostics Panel → build rows → diagnostics for translation unit
hacia el resto:
  T1 → Make Main Layout
  T1 → ensure buffer
  T2 → ISymbol Provider
  T2 → snapshot
  T3 → Make Console Panel
  T3 → Make Main Layout
