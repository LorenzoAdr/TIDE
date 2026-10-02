### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M1 M8
leído: load working lines from disk, save state, save workspace session, restore workspace session, flush active tab, save, open file
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia de la posición del cursor; lo leído confirma que se guardan y restauran los archivos abiertos paths y la pestaña activa, pero no la posición del cursor ni el scroll dentro de esos archivos
Abierto:
- run custom event drain
- run background generation

### Trabajo 2
consulta: dónde se guarda y restaura el estado de visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado de visibilidad de los paneles laterales al cerrar y abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la posición del cursor dentro del editor al cerrar y abrir el proyecto
keep: M8 M10
leído: set pos, reset to single cursor, save workspace session, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia de la posición del cursor; lo leído confirma que se guardan y restauran los archivos abiertos paths y la pestaña activa, pero no la posición del cursor ni el scroll dentro de esos archivos
Abierto:
- run custom event drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: load working lines from disk, save state, save workspace session, restore workspace session
    extra: open git diff view, open git diff tab

T2
    visto: (nada)

T3
    visto: set pos, reset to single cursor, save workspace session, restore workspace session
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: save workspace session
  T2=>T3  sin camino
hacia el resto:
  T1 → open git diff view
  T1 → open git diff tab
  T3 → cursor selection span on line
  T3 → ensure cursors
