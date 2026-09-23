### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir el proyecto
keep: M10
leído: load, remember
Cerrado:
leído: load, remember
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M5 M6
leído: save, load
Cerrado:
encontré el mecanismo de guardado y restauración: save serializa open tabs y active tab path a JSON en .tuide session.json, y load los lee de vuelta; el disparo de guardado al cerrar es begin shutdown y el de restauración al abrir es process pending workspace load
Abierto:
- run background generation
- begin shutdown
- process pending workspace load

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: load, remember
    extra: ensure wake fd, open host pty

T2
    visto: save, load
    extra: run background generation, update active environment

T3
    visto: (nada)

entre abiertas:
  T1=>T2  mismo objeto: load
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure wake fd
  T1 → open host pty
  T2 → run background generation
  T2 → update active environment
