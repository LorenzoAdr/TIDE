### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la posición del cursor al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al abrir un proyecto
keep: M3 M5
leído: load, save, config path, remember, reopen workspace documents
Cerrado:
encontré el mecanismo de guardado y restauración de la lista de proyectos recientes, pero no de la lista de archivos abiertos dentro del proyecto. Lo leído save persiste restaura rutas de directorios de trabajo workspaces en .config tide recent.json. La función reopen workspace documents no restaura archivos desde disco, sino que notifica al LSP los archivos ya cargados en memoria workspace- tabs . No hay evidencia de un mecanismo que guarde restore la lista de archivos abiertos tabs al abrir un proyecto.
Abierto:
- run background generation
- restart lsp for workspace
- load state
- save state
- open workspace

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M1 M12
leído: load, save, config path
Cerrado:
leído: load, save, config path
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: load, save, config path, remember
    extra: ensure wake fd, open host pty

T3
    visto: load, save, config path
    extra: ensure wake fd, open host pty

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: load
hacia el resto:
  T2 → ensure wake fd
  T2 → open host pty
  T3 → ensure wake fd
  T3 → open host pty
