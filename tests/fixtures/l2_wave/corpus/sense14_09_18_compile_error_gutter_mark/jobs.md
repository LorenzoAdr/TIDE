### Trabajo 1
consulta: dónde se extraen los errores de compilación del texto de la consola
keep: M6 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen del editor
keep: M1 M4
leído: draw gutter, draw line number, draw
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de la línea roja en el margen; lo leído son headers truncados de editor panel y lógica de folds en visual highlight, sin cuerpo de draw gutter ni draw line number
Abierto:
- current line indicator

### Trabajo 3
consulta: dónde se pinta el indicador de error en el margen del editor
keep: M1 M2
leído: line gutter marker, fold gutter marker, line diagnostic marker from map
Cerrado:
leído: line gutter marker, fold gutter marker, line diagnostic marker from map
Abierto:
- handle fold gutter click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: draw gutter, draw line number, draw
    extra: handle tabs overflow keys, editor content settled

T3
    visto: line gutter marker, fold gutter marker, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → handle tabs overflow keys
  T2 → editor content settled
  T3 → Make Editor Panel
  T3 → Make Main Layout
