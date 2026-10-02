### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: gutter buffer line at row, line gutter marker, handle gutter marker click
Cerrado:
leído: gutter buffer line at row, line gutter marker, handle gutter marker click
Abierto:
- handle editor mouse

### Trabajo 2
consulta: dónde se procesan los errores de compilación que aparecen en la consola inferior
keep: M12
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se procesan los errores de compilación que aparecen en la consola inferior
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se crea el marcador de error en el margen del editor
keep: M2
leído: line gutter marker, line diagnostic marker from map, diagnostics for editor line
Cerrado:
encontré el mecanismo de creación del marcador: line gutter marker delega en line diagnostic marker from map, que consulta diagnostics for editor line sobre panel- diagnostics by line; si hay un Diagnostic con severity k Error, retorna
Abierto:
- diagnostics by line update


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: gutter buffer line at row, line gutter marker, handle gutter marker click
    extra: handle editor mouse, Make Editor Panel

T2
    visto: (nada)

T3
    visto: line gutter marker, line diagnostic marker from map, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: line gutter marker
  T1=>T3  handle gutter marker click → line gutter marker → line diagnostic marker from map
  T2=>T3  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → Make Editor Panel
  T3 → Make Editor Panel
  T3 → Make Main Layout
