### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1
leído: Cursor Pos
Cerrado:
leído: Cursor Pos
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura el estado del editor al abrir el proyecto
keep: M1
leído: editor state cpp, restore workspace session, load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración del estado del editor cursor, selección, scroll; lo leído restaura la sesión de workspace tabs abiertos y programa de lanzamiento, pero no toca el estado interno del editor
Abierto:
- restore editor state
- load editor state

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M1
leído: Cursor Pos, editor state cpp, restore workspace session, set primary, set pos
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de la posición del cursor al abrir el proyecto; lo leído restaura las pestañas abiertas y el programa de lanzamiento, pero no toca el estado interno del editor cursor scroll
Abierto:
- restore cursor pos
- load editor state


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: Cursor Pos
    extra: cursor selection span on line, ensure cursors

T2
    visto: editor state cpp, restore workspace session, load
    extra: Application, set workspace

T3
    visto: Cursor Pos, editor state cpp, restore workspace session, set primary
    extra: cursor selection span on line, ensure cursors

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: Cursor Pos
  T2=>T3  mismo objeto: editor state cpp
hacia el resto:
  T1 → cursor selection span on line
  T1 → ensure cursors
  T2 → Application
  T2 → set workspace
  T3 → cursor selection span on line
  T3 → ensure cursors
