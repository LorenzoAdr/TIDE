### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
keep: M3 M7
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
keep: M1 M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda la posición del cursor al cerrar el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1 M4
leído: reset to single cursor, context menu open editor background, context menu open editor symbol
Cerrado:
leído: reset to single cursor, context menu open editor background, context menu open editor symbol
Abierto:
- handle editor mouse
- append menu item
- append doc comment items
- append debug watch items


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: (nada)

T3
    visto: reset to single cursor, context menu open editor background, context menu open editor symbol
    extra: set pos, handle editor mouse

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T3 → set pos
  T3 → handle editor mouse
