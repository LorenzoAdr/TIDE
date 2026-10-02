### Trabajo 1
consulta: dónde se pintan las marcas en el margen izquierdo del editor
keep: M1 M3
leído: build git marks snapshot, editor panel hpp, clear editor line paint caches, compute
Cerrado:
leído: build git marks snapshot, editor panel hpp, clear editor line paint caches, compute
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se parsean los errores de compilación; lo leído es exclusivamente el parsing de errores de linker undefined references en binary symbols panel
Abierto:
- parse compilation error
- parse compile error
- diagnostic parser
- clangd diagnostics

### Trabajo 3
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M1 M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se leen los errores de compilación de la consola para marcarlos en el editor
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré dónde se leen los errores de compilación de la consola para marcarlos en el editor; lo leído es exclusivamente el parsing de errores de linker en binary symbols panel y la gestión de selección arrastre en la consola, sin mecanismo de diagnóstico de compilación hacia el editor
Abierto:
- parse compiler errors
- diagnostics from console
- apply compilation errors


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: build git marks snapshot, editor panel hpp, clear editor line paint caches, compute
    extra: worker main, Visual Highlight Service

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, parse linker undefined reference

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → worker main
  T1 → Visual Highlight Service
  T2 → tick binary symbols panel
  T2 → parse linker undefined reference
