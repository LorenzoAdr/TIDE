### Trabajo 1
consulta: dónde se guarda y restaura el estado del editor al abrir un proyecto
keep: M1 M2
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura el estado del editor al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restaura la posición del cursor al abrir un proyecto
keep: M8 M3
leído: restore workspace session, rebuild display, open file
Cerrado:
leído: restore workspace session, rebuild display, open file
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restauran los archivos abiertos al volver a abrir un proyecto
keep: M1 M7
leído: load
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la restauración de archivos abiertos; lo leído load solo restaura la lista de proyectos recientes, no el estado de la sesión archivos abiertos .
Abierto:
- restore session
- open file


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: restore workspace session, rebuild display, open file
    extra: set workspace, Application

T3
    visto: load
    extra: open host pty, ensure wake fd

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → set workspace
  T2 → Application
  T3 → open host pty
  T3 → ensure wake fd
