### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el dibujo de la línea roja; lo leído son funciones que devuelven un carácter para el gutter, pero no el código que pinta ese carácter en la UI
Abierto:
- paint gutter
- draw line marker

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para extraer la línea exacta
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola inferior para extraer la línea exacta
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se pinta el carácter del gutter en la UI del editor
keep: M1
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
Cerrado:
leído: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen los errores de compilación del texto de la consola para crear diagnósticos
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: diagnostics for editor line, line diagnostic marker from map, line gutter marker
    extra: Make Editor Panel, Make Main Layout

T2
    visto: (nada)

T3
    visto: diagnostics for editor line, line diagnostic marker from map, line gutter marker, Make Editor Panel
    extra: Make Main Layout, git line changed

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: diagnostics for editor line
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → git line changed
  T3 → Make Main Layout
  T3 → git line changed
