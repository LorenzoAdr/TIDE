### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor al volver a abrir un proyecto
keep: M5 M4
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la posición del cursor al volver a abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la lista de archivos abiertos al volver a abrir un proyecto
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la lista de archivos abiertos al volver a abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al volver a abrir un proyecto
keep: M8 M9
leído: Split Separator Vertical, panel hpp
Cerrado:
leído: Split Separator Vertical, panel hpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: (nada)

T3
    visto: Split Separator Vertical, panel hpp
    extra: On Render, Accent Dim

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T3 → On Render
  T3 → Accent Dim
