### Trabajo 1
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostic severity color, diagnostics on line, Make Editor Panel, line diagnostic marker, line diagnostic marker from map, line gutter marker
Cerrado:
leído: diagnostics for editor line, diagnostic severity color, diagnostics on line, Make Editor Panel, line diagnostic marker, line diagnostic marker from map, line gutter marker
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
encontré el mecanismo: los errores no se leen de la consola, sino que provienen del LSP vía ISymbol Provider . El flujo es: ISymbol Provider notifica cambios → apply lsp diagnostics to panel verifica revisiones → push active file diagnostics from cache obtiene los datos de cached file diagnostics → rebuild diagnostics by line indexa los items por línea en panel diagnostics by line → diagnostics for editor line los devuelve al renderizado.
Abierto:
- handle editor mouse
- handle gutter marker click
- cached file diagnostics
- diagnostics revision

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1
leído: apply lsp diagnostics to panel, diagnostics on line, push active file diagnostics from cache, sync diagnostic cache
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré la lectura de errores de compilación de la consola; lo leído es el flujo de diagnostics LSP apply lsp diagnostics to panel, sync diagnostic cache, push active file diagnostics from cache que marca errores del lenguaje en el editor, no errores de compilación de la consola
Abierto:
- handle editor mouse
- handle gutter marker click
- scan shell output for linker errors
- console parser
- compile error from console


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, diagnostic severity color, diagnostics on line, Make Editor Panel
    extra: Make Main Layout, make diagnostic modal

T2
    visto: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed
    extra: Make Editor Panel, Make Main Layout

T3
    visto: apply lsp diagnostics to panel, diagnostics on line, push active file diagnostics from cache, sync diagnostic cache
    extra: ISymbol Provider, allows lsp ui

entre abiertas:
  T1=>T2  mismo objeto: diagnostics for editor line
  T1=>T2  Make Editor Panel → push active file diagnostics from cache → rebuild diagnostics by line if changed → rebuild diagnostics by line
  T1=>T3  mismo objeto: diagnostics on line
  T2=>T3  mismo objeto: diagnostics on line
hacia el resto:
  T1 → Make Main Layout
  T1 → make diagnostic modal
  T2 → Make Main Layout
  T2 → handle gutter marker click
  T3 → ISymbol Provider
  T3 → allows lsp ui
