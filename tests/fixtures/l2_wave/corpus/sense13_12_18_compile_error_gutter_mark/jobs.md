### Trabajo 1
consulta: dónde se marcan los errores de compilación con una línea roja en el margen izquierdo del editor
keep: M1
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, diagnostics for editor line, Make Editor Panel, handle gutter marker click
Cerrado:
leído: line gutter marker, diagnostics on line, line diagnostic marker from map, diagnostics for editor line, Make Editor Panel, handle gutter marker click
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para marcarlos en el editor
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola inferior para marcarlos en el editor
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crea el marcador de diagnóstico en el margen del editor
keep: M1
leído: line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el objeto de la consulta: el marcador se crea en line gutter marker 1005, que delega en line diagnostic marker from map para decidir el carácter o W basándose en la severidad de los diagnostics obtenidos vía diagnostics for editor line
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: line gutter marker, diagnostics on line, line diagnostic marker from map, diagnostics for editor line
    extra: Make Main Layout, git line changed

T2
    visto: (nada)

T3
    visto: line gutter marker, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T3 → Make Main Layout
  T3 → git line changed
