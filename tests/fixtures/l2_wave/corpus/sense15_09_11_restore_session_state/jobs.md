### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M6 M12
leído: save, save workspace session, restore workspace session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de guardado y restauración de archivos abiertos open tabs, active tab path en load y restore workspace session, pero NO encontré evidencia de que se guarde o restaure la posición del cursor cursor position offset . Lo leído confirma la persistencia de la lista de archivos y la pestaña activa, pero omite el estado del cursor.
Abierto:
- run background generation
- run custom event drain
- open file
- set cursor position

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo
keep: M1 M10
leído: open file at impl, open file at, reset to single cursor
Cerrado:
encontré el mecanismo de restauración de la posición del cursor al abrir un archivo: se realiza en open file at y open file at impl llamando a buffer reset to single cursor que a su vez invoca cursors front
Abierto:
- handle console panel mouse
- set pos

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al abrir el proyecto
keep: M9 M10
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save, save workspace session, restore workspace session
    extra: run background generation, update active environment

T2
    visto: open file at impl, open file at, reset to single cursor
    extra: open file at confirmed, flush active tab

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → run background generation
  T1 → update active environment
  T2 → open file at confirmed
  T2 → flush active tab
