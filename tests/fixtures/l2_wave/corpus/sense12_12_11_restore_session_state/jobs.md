### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
Cerrado:
encontré el mecanismo de restauración: al abrir un archivo, open file at impl y load buffer from disk para carga limpia invocan reset to single cursor que limpia los cursores y fija la posición primaria mediante set pos . La posición se restaura en la capa de workspace al confirmar la apertura, no en el editor de forma autónoma.
Abierto:
- set pos

### Trabajo 3
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
leído: Cursor Pos, editor state hpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se guarda el estado de los paneles laterales al cerrar el proyecto
keep: M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda el estado de los paneles laterales al cerrar el proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file at impl, load buffer from disk, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T4
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  sin camino
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
