### Trabajo 1
consulta: dónde se pintan las marcas de error en el margen del editor
keep: M1 M9
leído: collect diagnostic decorations, apply decoration
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de pintado en el margen gutter; lo leído son decoraciones de línea subrayado color en el texto . El sistema de margen gutter se gestiona en editor panel ej. line gutter marker handle gutter marker click pero no se ha leído su lógica de renderizado ni su conexión con los diagnostics.
Abierto:
- handle gutter marker click
- line gutter marker

### Trabajo 2
consulta: dónde se extraen los errores de compilación del texto de la consola para enviarlos al editor
keep: M8 M3
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola para enviarlos al editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se renderiza el marcador del margen izquierdo del editor
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se renderiza el marcador del margen izquierdo del editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: collect diagnostic decorations, apply decoration
    extra: Make Editor Panel, make sticky overlay

T2
    visto: (nada)

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Editor Panel
  T1 → make sticky overlay
