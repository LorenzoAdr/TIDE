### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cerrar y abrir el proyecto
keep: M1 M8
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardado restauración del estado del editor cursor archivos; lo leído save state serializa exclusivamente el estado de la sesión de IA turnos, acciones, pendientes, no el estado visual del editor.
Abierto:
- save editor state
- restore editor state

### Trabajo 2
consulta: dónde se persiste y recupera la posición del cursor y la lista de archivos abiertos al salir y entrar al proyecto
keep: M10 M11
leído: reset to single cursor, reset helix editor state, save workspace session, flush active tab, save
Cerrado:
leído: reset to single cursor, reset helix editor state, save workspace session, flush active tab, save
Abierto:
- handle editor keys
- run custom event drain
- run background generation

### Trabajo 3
consulta: dónde se guarda la posición del cursor al cerrar el editor
keep: M1 M9
leído: close, save
Cerrado:
leído: close, save
Abierto:
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: reset to single cursor, reset helix editor state, save workspace session, flush active tab
    extra: set pos, start command

T3
    visto: close, save
    extra: request start, discover repos

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: save
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → set pos
  T2 → start command
  T3 → request start
  T3 → discover repos
