### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al volver a abrir un proyecto
keep: M11 M6
leído: load, save
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de guardar restaurar la posición del cursor al volver a abrir un proyecto; lo leído save solo persiste una lista de rutas de proyectos recientes, sin estado del editor ni cursor
Abierto:
- run background generation
- save state
- restore state

### Trabajo 2
consulta: dónde se restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: M6 M1
leído: reopen workspace documents, run, load working lines from disk, remove tab mru, reload stale tabs from disk, open tabs mru
Cerrado:
leído: reopen workspace documents, run, load working lines from disk, remove tab mru, reload stale tabs from disk, open tabs mru
Abierto:
- restart lsp for workspace
- ensure backend started

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
keep: M4 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: load, save
    extra: ensure wake fd, open host pty

T2
    visto: reopen workspace documents, run, load working lines from disk, remove tab mru
    extra: restart lsp for workspace, on lsp missing install

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure wake fd
  T1 → open host pty
  T2 → restart lsp for workspace
  T2 → on lsp missing install
