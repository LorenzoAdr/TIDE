### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor state cpp, helix state cpp
Cerrado:
leído: editor state cpp, helix state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M5 M6
leído: load, save, remember
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de persistencia de la lista de archivos abiertos; lo leído recent projects gestiona la lista de proyectos recientes workspace roots, no los archivos abiertos dentro de la sesión
Abierto:
- begin shutdown
- process pending workspace load

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al cerrar y abrir el proyecto
keep: M1 M9
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
    visto: editor state cpp, helix state cpp

T2
    visto: load, save, remember
    extra: ensure wake fd, open host pty

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → ensure wake fd
  T2 → open host pty
