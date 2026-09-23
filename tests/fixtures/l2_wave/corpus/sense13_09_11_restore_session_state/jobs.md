### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M4
leído: save state, reset to single cursor, reset helix editor state
Cerrado:
leído: save state, reset to single cursor, reset helix editor state
Abierto:
- handle editor keys

### Trabajo 2
consulta: dónde se guarda la posición del cursor al cerrar el proyecto
keep: M2 M4
leído: save, save workspace session, save buffer
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de persistencia de la posición del cursor al cerrar el proyecto; lo leído muestra que save serializa solo open tabs lista de paths y active tab path pero no la posición del cursor offset linea columna dentro de esos archivos. El campo cursor position encontrado pertenece a search panel y text input style no al editor principal.
Abierto:
- run custom event drain
- cursor offset
- save state
- flush active tab

### Trabajo 3
consulta: dónde se restaura la posición del cursor al abrir el proyecto
keep: M8
leído: restore workspace session, open file
Cerrado:
leído: restore workspace session, open file
Abierto:
- handle navigation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state, reset to single cursor, reset helix editor state
    extra: state path, write file

T2
    visto: save, save workspace session, save buffer
    extra: session path, create directories

T3
    visto: restore workspace session, open file
    extra: Application, set workspace

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  save workspace session → active tab path → open file
hacia el resto:
  T1 → state path
  T1 → write file
  T2 → session path
  T2 → create directories
  T3 → Application
  T3 → set workspace
