### Trabajo 1
consulta: dónde se marcan los errores de compilación en la consola inferior
keep: M7
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se marcan los errores de compilación en la consola inferior; lo leído es un parser de errores de linker que solicita símbolos binarios, no un marcador visual en la consola
Abierto:
- render line
- apply compiler diagnostics
- mark error line

### Trabajo 2
consulta: dónde se dibuja una línea roja en el margen izquierdo del editor
keep: M1
leído: gutter buffer line at row, line gutter marker, handle gutter marker click
Cerrado:
leído: gutter buffer line at row, line gutter marker, handle gutter marker click
Abierto:
- handle editor mouse

### Trabajo 3
consulta: dónde se aplican los diagnósticos del compilador para marcar la línea de error en el editor
keep: M1
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Cerrado:
leído: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed, apply lsp diagnostics to panel, push active file diagnostics from cache
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 4
consulta: dónde se parsean los errores de compilación de la consola inferior para extraer la línea y el mensaje
keep: M11
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación de la consola inferior para extraer la línea y el mensaje
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se parsean los errores de compilación de la consola inferior para extraer la línea y el mensaje; lo leído es un parser de errores de linker binary symbols panel que busca símbolos binarios, no un sistema de parsing de errores de compilación gcc clang para extraer línea y mensaje
Abierto:
- parse compilation errors
- parse compiler output
- extract error line
- extract error message
- parse compilation error
- parse compiler error


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T2
    visto: gutter buffer line at row, line gutter marker, handle gutter marker click
    extra: handle editor mouse, Make Editor Panel

T3
    visto: diagnostics for editor line, diagnostics on line, rebuild diagnostics by line, rebuild diagnostics by line if changed
    extra: Make Editor Panel, Make Main Layout

T4
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T1=>T4  sin camino
  T2=>T3  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T2=>T4  sin camino
  T3=>T4  sin camino
hacia el resto:
  T1 → tick binary symbols panel
  T1 → has value
  T2 → handle editor mouse
  T2 → Make Editor Panel
  T3 → Make Editor Panel
  T3 → Make Main Layout
