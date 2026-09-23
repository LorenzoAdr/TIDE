### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino guardar y restaurar la posición del cursor
keep: M1
leído: Cursor Pos, editor state hpp, Editor Snapshot
Cerrado:
encontré el objeto de la consulta: hay camino guardar y restaurar la posición del cursor. El sistema es Editor Snapshot leído en ola 4, que contiene vector Multi Cursor cursors . Multi Cursor está compuesto por Cursor Pos leído en ola 2 3 implícitamente vía header y uso en . El camino de guardar es la creación de un Editor Snapshot que copia los cursores y su empuje a undo stack redo stack en Editor Buffer leído en ola 3 . El camino de restaurar es la extracción de un Editor Snapshot de esas pilas y la asignación de sus cursors al buffer. No hay métodos explícitos save cursor restore cursor pero la serialización restauración ocurre a nivel de snapshot del estado del editor.
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp, Editor Snapshot
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
