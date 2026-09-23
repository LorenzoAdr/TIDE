### Trabajo 1
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M2
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel, gutter buffer line at row, Event
Cerrado:
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel, gutter buffer line at row, Event
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola para obtener la línea del error
keep: M12
leído: parse file, parse sync
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de parsing de errores de compilación de la consola; lo leído son funciones de parsing de código fuente con Tree-sitter parse file, parse sync que construyen ASTs, no analizadores de output de compilador
Abierto:
- parse compiler errors
- extract error line
- console error parser


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, diagnostics on line, line diagnostic marker from map, Make Editor Panel
    extra: Make Main Layout, git line changed

T2
    visto: parse file, parse sync
    extra: tree sitter language for path, ts parser parse string

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T2 → tree sitter language for path
  T2 → ts parser parse string
