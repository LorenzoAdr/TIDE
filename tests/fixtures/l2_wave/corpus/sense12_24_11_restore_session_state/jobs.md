### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído define la estructura de datos en memoria Editor Buffer, Multi Cursor y funciones de manipulación, pero no hay evidencia de serialización, escritura a disco o guardado de estado al cerrar.
Abierto:
- on close editor
- save editor state

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un archivo
keep: M1
leído: open file at impl, open file impl
Cerrado:
encontré el mecanismo de restauración de la posición del cursor al abrir un archivo; lo leído muestra que open file at impl llama a buffer reset to single cursor tras abrir el archivo, estableciendo así la posición
Abierto:
- reset to single cursor

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restauran los archivos abiertos al abrir un proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: open file at impl, open file impl
    extra: open file at confirmed, reset to single cursor

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → open file at confirmed
  T2 → reset to single cursor
