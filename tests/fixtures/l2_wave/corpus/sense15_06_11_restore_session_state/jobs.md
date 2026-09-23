### Trabajo 1
consulta: dónde se guarda el estado del editor al cerrar el proyecto
keep: M1 M12
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se guarda el estado del editor al cerrar el proyecto; lo leído es la persistencia del estado de la sesión de IA Level2Session, que guarda turnos, acciones y pendientes, pero no el estado del editor búferes, folds, cursores .
Abierto:
- on close
- save editor state
- write editor state

### Trabajo 2
consulta: dónde se restaura el estado del editor al abrir un proyecto existente
keep: M1 M8
leído: Make Editor Panel, open completion, open
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se restaura el estado del editor al abrir un proyecto; lo leído es la creación de estado nuevo Make Editor Panel y la apertura de completado open completion, pero no hay mecanismo de carga de estado persistente del editor
Abierto:
- handle editor keys
- restore editor state
- load project state


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: Make Editor Panel, open completion, open
    extra: Make Main Layout, ensure buffer

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → Make Main Layout
  T2 → ensure buffer
