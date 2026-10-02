### Trabajo 1
consulta: dónde se guarda y restaura la posición del cursor y los archivos abiertos al volver a abrir un proyecto
keep: M1 M7
leído: workspace model cpp, editor state cpp
Cerrado:
leído: workspace model cpp, editor state cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se guarda y restaura la visibilidad de los paneles laterales al abrir un proyecto
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se guarda y restaura la posición del cursor al abrir un proyecto
keep: M7
leído: editor state cpp
Cerrado:
leído: editor state cpp
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: workspace model cpp, editor state cpp

T2
    visto: (nada)

T3
    visto: editor state cpp

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: editor state cpp
  T2=>T3  sin camino
