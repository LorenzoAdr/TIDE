### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: gutter buffer line at row, line gutter marker, diagnostics on line, line diagnostic marker from map
Cerrado:
encontré el mecanismo: la línea roja carácter se decide en line diagnostic marker from map 978 993 cuando hay un diagnóstico de severidad k Error y se expone vía line gutter marker 1005 1017 . El dibujo real en el margen se gestiona en el sistema de pintura del gutter del editor panel, pero la lógica de qué marcar como error está anclada y leída.
Abierto:
- handle editor mouse
- pintura pixel gutter

### Trabajo 2
consulta: dónde se convierten los errores de compilación de la consola en diagnósticos del editor
keep: M1 M7
leído: scan shell output for linker errors, diagnostics on line, line diagnostic marker from map, diagnostics for editor line
Cerrado:
leído: scan shell output for linker errors, diagnostics on line, line diagnostic marker from map, diagnostics for editor line
Abierto:
- handle editor mouse
- handle gutter marker click

### Trabajo 3
consulta: dónde se actualiza el mapa de diagnósticos del editor al detectar un error en la consola
keep: M1
leído: diagnostics on line, line diagnostic marker from map, diagnostics for editor line, line gutter marker, rebuild diagnostics by line, rebuild diagnostics by line if changed
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de actualización del mapa de diagnósticos del editor al detectar un error en la consola; lo leído muestra que el mapa se actualiza vía LSP apply lsp diagnostics to panel - rebuild diagnostics by line if changed, pero no hay evidencia de un flujo desde la consola shell output hacia este mapa. La función candidata para errores de consola scan shell output for linker errors pertenece a binary symbols panel y no se conecta al editor panel
Abierto:
- handle editor mouse
- handle gutter marker click
- scan shell output for linker errors
- apply lsp diagnostics to panel

### Trabajo 4
consulta: dónde se parsean los errores de compilación del texto de la consola inferior
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de parsing de errores de compilación de la consola inferior; lo leído es un parser específico para errores de linker undefined references en el panel de símbolos binarios, no el sistema general de errores de compilación
Abierto:
- parse compilation errors
- parse errors
- apply shell diagnostics


# control_opened_v1
n=4  (visto+hops extra+circuito; sin código)

T1
    visto: gutter buffer line at row, line gutter marker, diagnostics on line, line diagnostic marker from map
    extra: handle editor mouse, Make Editor Panel

T2
    visto: scan shell output for linker errors, diagnostics on line, line diagnostic marker from map, diagnostics for editor line
    extra: tick binary symbols panel, has value

T3
    visto: diagnostics on line, line diagnostic marker from map, diagnostics for editor line, line gutter marker
    extra: handle gutter marker click, handle editor mouse

T4
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

entre abiertas:
  T1=>T2  mismo objeto: diagnostics on line
  T1=>T2  line gutter marker → line diagnostic marker from map → diagnostics for editor line
  T1=>T3  mismo objeto: line gutter marker
  T1=>T4  sin camino
  T2=>T3  mismo objeto: diagnostics on line
  T2=>T4  mismo objeto: scan shell output for linker errors
  T3=>T4  sin camino
hacia el resto:
  T1 → handle editor mouse
  T1 → Make Editor Panel
  T2 → tick binary symbols panel
  T2 → has value
  T3 → handle gutter marker click
  T3 → handle editor mouse
