### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M1 M2
leído: save workspace session, restore workspace session, flush active tab, app settings hpp, Workspace Session, Tab
Cerrado:
encontré el mecanismo de persistencia de archivos abiertos Workspace Session en guardado en save workspace session y restaurado en restore workspace session, pero confirmé que NO se guarda ni restaura la posición del cursor: Workspace Session solo almacena paths de tabs y active tab path; flush active tab solo guarda el buffer de texto; la estructura Tab no tiene campo de posición de cursor y no se serializa.
Abierto:
- run custom event drain

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor del editor al cerrar y abrir el proyecto
keep: M6
leído: save workspace session, restore workspace session, reset to single cursor, Workspace Session
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor; lo leído muestra que save workspace session y restore workspace session solo persisten y abren rutas de archivos open tabs active tab path sin coordenadas de cursor. Workspace Session no tiene campos de posición. reset to single cursor es un efecto de reseteo, no de restauración de sesión.
Abierto:
- run custom event drain
- save cursor state
- restore cursor state

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save workspace session, restore workspace session, flush active tab, app settings hpp
    extra: run custom event drain, apply pending connection

T2
    visto: save workspace session, restore workspace session, reset to single cursor, Workspace Session
    extra: run custom event drain, apply pending connection

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: save workspace session
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → run custom event drain
  T1 → apply pending connection
  T2 → run custom event drain
  T2 → apply pending connection
