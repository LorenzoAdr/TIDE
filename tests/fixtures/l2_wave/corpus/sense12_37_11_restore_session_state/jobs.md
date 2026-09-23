### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1
leído: save state, load state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la persistencia del estado del editor; lo leído son verbos de guardado carga del estado de la sesión de IA Level2Session, que serializan turnos, acciones y hunks pendientes, pero no el estado del editor cursor, scroll, etc.
Abierto:
- restore state

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir un proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir un proyecto
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir un proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state, load state
    extra: state path, write file

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → cursor selection span on line
  T2 → ensure cursors
