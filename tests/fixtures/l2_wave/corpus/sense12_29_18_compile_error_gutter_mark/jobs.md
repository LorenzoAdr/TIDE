### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola inferior
keep: M7
leído: scan shell output for linker errors, tick binary symbols panel
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de lectura de errores de compilación en la consola; lo leído es un escáner específico de errores de linker undefined references en binary symbols panel, no un parser genérico de errores de compilación para la consola inferior
Abierto:
- parse compilation errors
- on output
- feed output

### Trabajo 2
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor
keep: M1
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, Make Editor Panel
Cerrado:
leído: line gutter marker, gutter buffer line at row, line diagnostic marker from map, Make Editor Panel
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se extraen la línea y el mensaje de los errores de compilación del texto de la consola
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 4
consulta: dónde se parsean los errores de compilación del texto de la consola para obtener la línea y el mensaje
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para obtener la línea y el mensaje
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors, tick binary symbols panel
    extra: has value, parse linker undefined reference

T2
    visto: line gutter marker, gutter buffer line at row, line diagnostic marker from map, Make Editor Panel
    extra: Make Main Layout, git line changed

T3
    visto: (nada)

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  sin camino
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → has value
  T1 → parse linker undefined reference
  T2 → Make Main Layout
  T2 → git line changed
