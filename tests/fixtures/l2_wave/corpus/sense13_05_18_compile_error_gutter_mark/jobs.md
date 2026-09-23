### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M11
leído: console panel cpp, parse pty filter tokens, row matches pty filter
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de errores de compilación en la salida de la consola; lo leído console panel solo implementa filtrado de texto por tokens pty filter y renderizado, sin lógica de parsing de errores de compilador
Abierto:
- parse compile errors
- count workspace diagnostics

### Trabajo 2
consulta: dónde se parsean los errores de compilación del texto de la consola
keep: M12
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se marcan los errores de compilación con una línea roja en el margen del editor
keep: M9 M10
leído: apply visual highlight fold regions, mark visual highlight dirty
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de marcaje de errores de compilación con línea roja en el margen; lo leído visual highlight cpp implementa exclusivamente el plegado de código code folding y la invalidación de estado visual asociada, sin lógica de diagnóstico ni renderizado de errores en el gutter
Abierto:
- gutter error renderer
- diagnostic to gutter marker
- apply diagnostics visual


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: console panel cpp, parse pty filter tokens, row matches pty filter
    extra: build console panel view, render app terminal

T2
    visto: (nada)

T3
    visto: apply visual highlight fold regions, mark visual highlight dirty
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → build console panel view
  T1 → render app terminal
  T3 → Make Editor Panel
  T3 → Make Main Layout
