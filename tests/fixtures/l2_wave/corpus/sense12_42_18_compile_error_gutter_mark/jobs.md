### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M7
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que detecta errores de compilación en la salida de la consola; lo leído es scan shell output for linker errors que solo parsea referencias indefinidas de linker, no errores de compilación syntax type errors .
Abierto:
- parse compilation errors
- console error detection

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen del editor para marcar un error
keep: M1
leído: diagnostic severity color, diagnostics for editor line, Make Editor Panel, line gutter marker, handle gutter marker click
Cerrado:
leído: diagnostic severity color, diagnostics for editor line, Make Editor Panel, line gutter marker, handle gutter marker click
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se parsean los errores de compilación de la salida del compilador en la consola
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación de la salida del compilador en la consola
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: diagnostic severity color, diagnostics for editor line, Make Editor Panel, line gutter marker
    extra: Make Main Layout, make diagnostic modal

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T2 → Make Main Layout
  T2 → make diagnostic modal
