### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda la selección del cursor al cerrar el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se restaura la selección del cursor al abrir el editor
keep: M1
leído: Cursor Pos, open file at impl, reset to single cursor
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de restauración de la posición del cursor al abrir el editor: open file at impl llama a reset to single cursor que colapsa la selección y fija la posición en . No encontré lógica para restaurar la selección rango completa, solo la posición puntual.
Abierto:
- restore selection range


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T4
    visto: Cursor Pos, open file at impl, reset to single cursor
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  mismo objeto: Cursor Pos
  T3=>T4  mismo objeto: Cursor Pos
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
