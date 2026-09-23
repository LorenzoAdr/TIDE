### Trabajo 1
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1
leído: Cursor Pos, editor state hpp
Cerrado:
Lo leído no contestó ESA pregunta. Un hop de Abierto/extra no es un hueco.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el editor; lo leído Editor Buffer y Cursor Pos define el estado en memoria línea columna pero no revela dónde se serializa ni qué dispara el guardado al cerrar, ya que las búsquedas de verbos de cierre guardado fallaron
Abierto:
- save state
- on close
- serialize cursor

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir el editor
keep: M1
leído: context menu open editor background, context menu open editor symbol, open file at impl, open file at confirmed, reset to single cursor
Cerrado:
leído: context menu open editor background, context menu open editor symbol, open file at impl, open file at confirmed, reset to single cursor
Abierto:
- handle editor mouse
- append menu item
- append doc comment items
- append debug watch items

### Trabajo 3
consulta: dónde se serializa el estado de la sesión al cerrar el proyecto para restaurar cursor y paneles
keep: M3
leído: save workspace session, save, flush active tab, Workspace Session
Cerrado:
leído: save workspace session, save, flush active tab, Workspace Session
Abierto:
- run custom event drain


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos, editor state hpp
    extra: cursor selection span on line, ensure cursors

T2
    visto: context menu open editor background, context menu open editor symbol, open file at impl, open file at confirmed
    extra: handle editor mouse, append doc comment items

T3
    visto: save workspace session, save, flush active tab, Workspace Session
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  extra toca visto: flush active tab
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → handle editor mouse
  T2 → append doc comment items
  T3 → run custom event drain
  T3 → apply pending connection
