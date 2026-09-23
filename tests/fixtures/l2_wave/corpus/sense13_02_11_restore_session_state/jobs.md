### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: editor text hpp, workspace model cpp, editor panel cpp
Cerrado:
leído: editor text hpp, workspace model cpp, editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al cerrar y abrir el proyecto
keep: M5 M6
leído: save, load
Cerrado:
encontré el mecanismo de guardado y restauración: save y load persisten restauran el vector open tabs lista de archivos abiertos y active tab path en un archivo JSON workspace session json dentro de la raíz del workspace.
Abierto:
- run background generation
- open tabs

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
    visto: editor text hpp, workspace model cpp, editor panel cpp

T2
    visto: save, load
    extra: run background generation, update active environment

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → run background generation
  T2 → update active environment
