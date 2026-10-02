### Trabajo 1
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor para marcar un error
keep: M2
leído: line gutter marker, line diagnostic marker from map, Make Editor Panel
Cerrado:
leído: line gutter marker, line diagnostic marker from map, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para extraer la línea del error
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola inferior para extraer la línea del error
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee los errores de compilación de la consola inferior; lo leído es el sistema de depuración dap backend que gestiona la ejecución del inferior, no el parsing de la salida de compilación
Abierto:
- console parser
- compile error extractor
- inferior console handler

### Trabajo 3
consulta: dónde se parsea la salida de la consola para extraer errores de compilación y su línea
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsea la salida de la consola para extraer errores de compilación y su línea
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, line diagnostic marker from map, Make Editor Panel
    extra: Make Main Layout, git line changed

T2
    visto: (nada)

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
