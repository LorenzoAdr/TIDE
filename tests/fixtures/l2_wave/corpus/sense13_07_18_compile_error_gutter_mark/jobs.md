### Trabajo 1
consulta: dónde se dibujan las marcas en el margen izquierdo del editor
keep: M1
leído: line gutter marker, fold gutter marker, Make Editor Panel, line diagnostic marker from map, git line changed
Cerrado:
encontré el objeto de la consulta: las marcas se calculan en line gutter marker 1005 que delega en line diagnostic marker from map diagnostics y git line changed git, y se asignan al render en el bucle de Make Editor Panel 7310 dentro de for
Abierto:
- handle fold gutter click
- line ai authored

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M2 M4
leído: line diagnostic marker from map, diagnostics for translation unit, parse gcc style, terminal row link, find compiler location, diagnostics for editor line, render terminal styled, push active file diagnostics from cache
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído muestra dos sistemas independientes: 1 LSP llena panel diagnostics by line vía push active file diagnostics from cache no consola, y 2 la consola parsea ubicaciones find compiler location solo para navegación al hacer clic open terminal link sin escribir en el estado de diagnósticos del editor.
Abierto:
- apply lsp diagnostics to panel
- cached file diagnostics

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la consola para extraer la ubicación
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación de la consola para extraer la ubicación
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, fold gutter marker, Make Editor Panel, line diagnostic marker from map
    extra: Make Main Layout, handle fold gutter click

T2
    visto: line diagnostic marker from map, diagnostics for translation unit, parse gcc style, terminal row link
    extra: Make Editor Panel, line gutter marker

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: line diagnostic marker from map
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → handle fold gutter click
  T2 → Make Main Layout
  T2 → build include tree
