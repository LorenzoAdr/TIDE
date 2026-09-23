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
leído: Cursor Pos, open file impl, open new tab from disk, switch to tab, load buffer from disk
Cerrado:
leído: Cursor Pos, open file impl, open new tab from disk, switch to tab, load buffer from disk
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino guardar la posición del cursor y restaurarla al abrir
keep: M1
leído: Cursor Pos, editor state hpp, set primary, primary line, primary col
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el camino de guardar restaurar la posición del cursor al abrir; lo leído confirma que no hay flujo directo desde open file impl a set primary, y el sistema Editor Buffer no expone mecanismos de persistencia de cursor en el header ni en los verbos leídos
Abierto:
- handle editor keys
- save cursor position
- restore cursor position
- open file impl

### Trabajo 4
consulta: dónde se guarda el estado de la sesión al cerrar el proyecto
keep: M1
leído: save workspace session, save, flush active tab, Workspace Session, workspace model hpp
Cerrado:
leído: save workspace session, save, flush active tab, Workspace Session, workspace model hpp
Abierto:
- run custom event drain
- run background generation


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: Cursor Pos, open file impl, open new tab from disk, switch to tab
    extra: cursor selection span on line, ensure cursors

T3
    visto: Cursor Pos, editor state hpp, set primary, primary line
    extra: cursor selection span on line, ensure cursors

T4
    visto: save workspace session, save, flush active tab, Workspace Session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  mismo objeto: Cursor Pos
  T1=>T3  mismo objeto: Cursor Pos
  T1=>T4  sin camino
  T2=>T3  mismo objeto: Cursor Pos
  T2=>T4  save workspace session → active tab path → open file → open file impl
  T3=>T4  sin camino
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → cursor selection span on line
  T2 → ensure cursors
  T3 → cursor selection span on line
  T3 → ensure cursors
