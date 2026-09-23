### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M6 M7
leído: line gutter marker, gutter buffer line at row
Cerrado:
leído: line gutter marker, gutter buffer line at row
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M9
leído: count workspace diagnostics, apply fix for row
Cerrado:
leído: count workspace diagnostics, apply fix for row
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crea la decoración de error en el margen al recibir un diagnóstico de compilación
keep: M1 M2
leído: sync diagnostic cache, cached file diagnostics, apply lsp diagnostics to panel, push active file diagnostics from cache, invalidate editor view
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la creación de la decoración de error en el margen; lo leído es la invalidación del cache de renderizado invalidate editor view y la gestión de datos de diagnóstico sync diagnostic cache, push active file diagnostics from cache, pero no el código que instancia o dibuja la decoración visual en el gutter
Abierto:
- handle editor mouse
- draw gutter diagnostic
- create margin decoration
- render diagnostic marker


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, gutter buffer line at row
    extra: Make Editor Panel, Make Main Layout

T2
    visto: count workspace diagnostics, apply fix for row
    extra: Make Main Layout, count errors

T3
    visto: sync diagnostic cache, cached file diagnostics, apply lsp diagnostics to panel, push active file diagnostics from cache
    extra: ISymbol Provider, snapshot

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sync diagnostic cache → count workspace diagnostics
hacia el resto:
  T1 → Make Editor Panel
  T1 → Make Main Layout
  T2 → Make Main Layout
  T2 → count errors
  T3 → ISymbol Provider
  T3 → snapshot
