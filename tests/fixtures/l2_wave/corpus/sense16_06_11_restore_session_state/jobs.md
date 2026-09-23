### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1 M2
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el guardado del estado del editor al cerrar el proyecto; lo leído es el guardado del estado de la sesión de IA Level2Session, que persiste turnos, acciones y pendientes de edición, no el estado UI del editor scroll, cursores, paneles .
Abierto:
- persist state
- project close handler

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M8 M10
leído: restore workspace session, open file
Cerrado:
leído: restore workspace session, open file
Abierto:
- handle navigation

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al abrir un proyecto
keep: M1 M7
leído: reopen workspace documents, run, flush active tab, apply connection and start, load
Cerrado:
leído: reopen workspace documents, run, flush active tab, apply connection and start, load
Abierto:
- restart lsp for workspace
- run custom event drain
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: restore workspace session, open file
    extra: set workspace, Application

T3
    visto: reopen workspace documents, run, flush active tab, apply connection and start
    extra: restart lsp for workspace, on lsp missing install

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  begin insert at → pending insert → run insert async → bootstrap level2 session → bootstrap → save state
  T2=>T3  set workspace → restore workspace session
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → set workspace
  T2 → Application
  T3 → restart lsp for workspace
  T3 → on lsp missing install
