### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cargar un proyecto
keep: M1 M11
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar el estado del editor al cargar un proyecto; lo leído save state en level2 session persiste el estado de la sesión de IA turnos, acciones, pendientes, no el estado visual lógico del editor cursors, scroll, buffers .
Abierto:
- load project
- restore editor state
- save editor state

### Trabajo 2
consulta: dónde se guarda la posición del cursor y los archivos abiertos al cerrar el editor
keep: M8 M2
leído: save workspace session, workspace config hpp, flush active tab, save
Cerrado:
leído: save workspace session, workspace config hpp, flush active tab, save
Abierto:
- run custom event drain

### Trabajo 3
consulta: dónde se restauran los archivos abiertos y la posición del cursor al cargar la sesión del workspace
keep: M5 M9
leído: load, session path
Cerrado:
leído: load, session path
Abierto:
- run inotify loop


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: save workspace session, workspace config hpp, flush active tab, save
    extra: run custom event drain, apply pending connection

T3
    visto: load, session path
    extra: start scan, ensure wake fd

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → run custom event drain
  T2 → apply pending connection
  T3 → start scan
  T3 → ensure wake fd
