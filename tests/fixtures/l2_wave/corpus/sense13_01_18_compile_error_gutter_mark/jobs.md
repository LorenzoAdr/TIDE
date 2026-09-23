### Trabajo 1
consulta: dónde se parsean los errores de compilación del texto de la consola
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se pinta la línea roja en el margen del editor
keep: M1 M3
leído: line gutter marker, gutter buffer line at row, diagnostics on line, line diagnostic marker from map, git line changed, diagnostics for editor line
Cerrado:
encontré el mecanismo de decisión del marcador: line gutter marker decide pintar si line diagnostic marker from map retorna cuando hay un diagnostic con severity k Error . La línea roja es este carácter en el gutter. No he leído la función de dibujo final pintado de píxeles caracteres en la UI, pero el origen lógico está anclado.
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se crea el diagnostic con severity k Error que pinta la línea roja
keep: M1
leído: line diagnostic marker from map, diagnostics on line, diagnostics for editor line, line gutter marker, diagnostic severity color
Cerrado:
encontré el objeto de la consulta: la línea roja marcador se crea en line diagnostic marker from map cuando k Error el color rojo visual se asigna en diagnostic severity color retornando Error para esa severidad
Abierto:
- handle editor mouse
- handle gutter marker click


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: line gutter marker, gutter buffer line at row, diagnostics on line, line diagnostic marker from map
    extra: Make Editor Panel, Make Main Layout

T3
    visto: line diagnostic marker from map, diagnostics on line, diagnostics for editor line, line gutter marker
    extra: Make Main Layout, Make Editor Panel

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: line gutter marker
hacia el resto:
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → Make Main Layout
  T3 → Make Editor Panel
