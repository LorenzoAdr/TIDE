### Trabajo 1
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl
Cerrado:
leído: Cursor Pos, open file at impl
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la selección del cursor al abrir un proyecto
keep: M1
leído: Cursor Pos, open file at impl, reset to single cursor
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de la selección span al abrir un proyecto; lo leído muestra que open file at impl llama a reset to single cursor la cual colapsa explícitamente el cursor cursors clear descartando cualquier selección previa en lugar de restaurarla
Abierto:
- restore selection on open
- open file at confirmed

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restauran los archivos abiertos al abrir un proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restauran los paneles laterales al abrir un proyecto
keep: M2
leído: main layout cpp
Cerrado:
leído: main layout cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, open file at impl
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file at impl, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

T4
    visto: main layout cpp

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
