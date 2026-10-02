### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
keep: M3 M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la posición del cursor en el editor al cerrar el proyecto
keep: M1 M12
leído: close, save state, reset to single cursor, handle editor keys
Cerrado:
leído: close, save state, reset to single cursor, handle editor keys
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M5 M6
leído: save, load
Cerrado:
encontré el mecanismo: save y load persisten restauran la lista de archivos abiertos en el campo JSON open tabs del archivo de sesión workspace session json .
Abierto:
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: close, save state, reset to single cursor, handle editor keys
    extra: spawn stdio adapter, request start

T3
    visto: save, load
    extra: run background generation, update active environment

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → spawn stdio adapter
  T2 → request start
  T3 → run background generation
  T3 → update active environment
