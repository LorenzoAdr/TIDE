### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
keep: M3 M10
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M7
leído: close, set pos, reset to single cursor
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído muestra que close solo limpia estado UI y reset to single cursor solo establece posición en memoria volátil
Abierto:
- run git
- run git argv
- save state
- load state

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1 M10
leído: context menu open editor background, context menu open editor symbol, open file at impl, open file at
Cerrado:
leído: context menu open editor background, context menu open editor symbol, open file at impl, open file at
Abierto:
- handle editor mouse
- handle console panel mouse
- append menu item
- append doc comment items
- append debug watch items


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: close, set pos, reset to single cursor
    extra: request start, discover repos

T3
    visto: context menu open editor background, context menu open editor symbol, open file at impl, open file at
    extra: handle editor mouse, is lsp trackable path

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  extra toca visto: reset to single cursor
hacia el resto:
  T2 → request start
  T2 → discover repos
  T3 → handle editor mouse
  T3 → is lsp trackable path
