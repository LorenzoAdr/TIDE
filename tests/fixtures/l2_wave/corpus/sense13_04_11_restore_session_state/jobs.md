### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close, set pos
Cerrado:
leído: close, set pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1 M4
leído: reset to single cursor, open file at impl, on opened
Cerrado:
encontré el mecanismo: la posición se restaura en open file at impl, que invoca buffer reset to single cursor line, col tras cargar el archivo
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: close, set pos
    extra: request start, App Session

T3
    visto: reset to single cursor, open file at impl, on opened
    extra: set pos, open file at confirmed

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  reset to single cursor → set pos
hacia el resto:
  T2 → request start
  T2 → App Session
  T3 → open file at confirmed
  T3 → flush active tab
