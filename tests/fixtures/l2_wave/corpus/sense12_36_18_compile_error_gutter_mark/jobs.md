### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se pinta la línea roja en el margen izquierdo del editor
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se marcan los errores de compilación en el margen del editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Cerrado:
leído: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click, line diagnostic marker from map
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se crea el marcador de diagnóstico en el margen del editor
keep: M1
leído: line diagnostic marker from map, diagnostics on line, line gutter marker, diagnostics for editor line
Cerrado:
encontré dónde se crea el marcador: en line gutter marker 1005, que delega en line diagnostic marker from map para obtener el carácter o W y lo retorna para su dibujo en el gutter
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 4
consulta: dónde se leen los errores de compilación de la consola para crear los diagnósticos
keep: M1
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la lectura de errores de compilación de la consola para diagnósticos; lo leído muestra que los diagnósticos del editor provienen de LSP Document Diagnostics y los errores de shell se usan solo para símbolos de binarios linker, no para diagnósticos de línea en el editor
Abierto:
- handle editor mouse
- handle gutter marker click
- parse compilation errors
- update diagnostics from console


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: diagnostics for editor line, diagnostics on line, line gutter marker, handle gutter marker click
    extra: Make Editor Panel, Make Main Layout

T3
    visto: line diagnostic marker from map, diagnostics on line, line gutter marker, diagnostics for editor line
    extra: Make Editor Panel, Make Main Layout

T4
    visto: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, scan shell output for linker errors
    extra: Make Editor Panel, Make Main Layout

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  mismo objeto: diagnostics for editor line
  T2=>T4  mismo objeto: diagnostics for editor line
  T3=>T4  mismo objeto: diagnostics on line
hacia el resto:
  T2 → Make Editor Panel
  T2 → Make Main Layout
  T3 → Make Editor Panel
  T3 → Make Main Layout
  T4 → Make Editor Panel
  T4 → Make Main Layout
