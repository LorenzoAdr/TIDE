### Trabajo 1
consulta: dónde se parsean los errores de compilación de la salida de la consola
keep: M8
leído: std, parse pty filter tokens
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de parsing de errores de compilación; lo leído son utilidades de texto trim, lower y parsing de filtros de terminal, no de diagnósticos del compilador
Abierto:
- handle compiler output
- parse compilation errors
- diagnostic parser

### Trabajo 2
consulta: dónde se dibujan las marcas visuales en el margen izquierdo del editor
keep: M1
leído: line gutter marker, gutter buffer line at row, fold gutter marker
Cerrado:
leído: line gutter marker, gutter buffer line at row, fold gutter marker
Abierto:
- handle editor mouse
- handle fold gutter click

### Trabajo 3
consulta: dónde se leen y procesan los errores de compilación de la salida del terminal
keep: M6
leído: compiler location cpp, diagnostics panel cpp
Cerrado:
leído: compiler location cpp, diagnostics panel cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: std, parse pty filter tokens
    extra: contains, trim copy

T2
    visto: line gutter marker, gutter buffer line at row, fold gutter marker
    extra: Make Editor Panel, Make Main Layout

T3
    visto: compiler location cpp, diagnostics panel cpp

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → contains
  T1 → trim copy
  T2 → Make Editor Panel
  T2 → Make Main Layout
