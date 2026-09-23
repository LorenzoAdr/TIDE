### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M1
leído: Cursor Pos, editor state hpp, editor text hpp
Cerrado:
leído: Cursor Pos, editor state hpp, editor text hpp
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
leído: Cursor Pos, editor state hpp
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré camino de guardar y restaurar la posición del cursor; lo leído son métodos de normalización de rango, selección y acceso a campos col, set primary sin serialización ni persistencia
Abierto:
- save cursor pos
- restore cursor pos
- serialize cursor

### Trabajo 4
consulta: dónde se guardan los archivos abiertos al cerrar el proyecto
keep: M1
leído: save workspace session, restore workspace session, flush active tab, open file
Cerrado:
leído: save workspace session, restore workspace session, flush active tab, open file
Abierto:
- run custom event drain
- handle navigation


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp, editor text hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T4
    visto: save workspace session, restore workspace session, flush active tab, open file
    extra: run custom event drain, apply pending connection

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
