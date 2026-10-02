### Trabajo 1
consulta: dónde se pinta la línea roja en el margen izquierdo del editor para marcar errores
keep: M1
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se pinta la línea roja en el margen izquierdo del editor para marcar errores
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola inferior para marcarlos en el editor
keep: M1 M7
leído: scan shell output for linker errors, diagnostics for translation unit, diagnostics for editor line
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola inferior; lo leído son funciones de filtrado de diagnostics LSP diagnostics for translation unit y parseo de errores de linker scan shell output for linker errors, pero no el parseo de errores de compilación gcc clang desde la consola
Abierto:
- parse compiler errors from console
- console diagnostics parser


# control_opened_v1
n=2  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: scan shell output for linker errors, diagnostics for translation unit, diagnostics for editor line
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
hacia el resto:
  T2 → tick binary symbols panel
  T2 → has value
