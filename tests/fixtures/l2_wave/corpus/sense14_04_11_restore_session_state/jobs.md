### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al cargar un proyecto
keep: M4
leído: save state
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar posición del cursor y archivos abiertos al cargar un proyecto; lo leído save state persiste estado de sesión AI turnos, acciones, pendientes, no estado de UI editor
Abierto:
- restore state

### Trabajo 2
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cargar un proyecto
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la visibilidad de los paneles laterales al cargar un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda la posición del cursor y los archivos abiertos del editor al cerrar el proyecto
keep: M1 M9
leído: save workspace session, flush active tab, save
Cerrado:
leído: save workspace session, flush active tab, save
Abierto:
- run custom event drain
- run background generation


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: save state
    extra: state path, write file

T2
    visto: (nada)

T3
    visto: save workspace session, flush active tab, save
    extra: run custom event drain, apply pending connection

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → state path
  T1 → write file
  T3 → run custom event drain
  T3 → apply pending connection
