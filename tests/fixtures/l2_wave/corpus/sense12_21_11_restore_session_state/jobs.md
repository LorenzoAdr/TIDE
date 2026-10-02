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
leído: Cursor Pos, open file at impl, load buffer from lines, reset to single cursor
Cerrado:
encontré el mecanismo de restauración: al abrir un archivo, open file at impl workspace model invoca buffer reset to single cursor el cual limpia los cursores y establece la posición vía set pos en editor state .
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la lista de archivos abiertos al reabrir el proyecto
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la lista de archivos abiertos al reabrir el proyecto
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, amplia otro keep o un hop del pack; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se guarda el estado de los paneles laterales al cerrar el proyecto
keep: M2
leído: save state, load state, main layout hpp
Cerrado:
leído: save state, load state, main layout hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file at impl, load buffer from lines, reset to single cursor
    extra: cursor selection span on line, ensure cursors

T3
    visto: (nada)

T4
    visto: save state, load state, main layout hpp
    extra: Make Busy Strip Placeholder, colors revision

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
  T4 → Make Busy Strip Placeholder
  T4 → colors revision
