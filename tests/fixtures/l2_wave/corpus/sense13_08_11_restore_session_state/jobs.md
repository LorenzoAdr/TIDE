### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M8
leído: reset to single cursor
Cerrado:
leído: reset to single cursor
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se restauran los archivos abiertos al volver a abrir un proyecto
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se restauran los archivos abiertos al volver a abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
keep: M8
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
    visto: reset to single cursor
    extra: set pos, start command

T2
    visto: (nada)

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → set pos
  T1 → start command
