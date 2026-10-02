### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
keep: M1 M9
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al cerrar y abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la posición del cursor al cerrar y abrir un proyecto
keep: M2 M9
leído: remember, load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor al cerrar abrir un proyecto; lo leído load solo persiste la lista de rutas de workspaces recientes, sin estado de cursor
Abierto:
- restore cursor position
- save workspace state
- save cursor

### Trabajo 3
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir un proyecto
keep: M5 M6
leído: load working lines from disk, remove tab mru, open tabs mru, close tab
Cerrado:
leído: load working lines from disk, remove tab mru, open tabs mru, close tab
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: remember, load
    extra: is directory, normalize workspace path

T3
    visto: load working lines from disk, remove tab mru, open tabs mru, close tab
    extra: open git diff view, open git diff tab

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → is directory
  T2 → normalize workspace path
  T3 → open git diff view
  T3 → open git diff tab
