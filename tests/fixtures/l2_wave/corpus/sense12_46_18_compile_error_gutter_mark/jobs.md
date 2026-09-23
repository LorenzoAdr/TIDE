### Trabajo 1
consulta: dónde se dibuja la línea roja en el margen del editor para marcar errores
keep: M1
leído: clear editor line paint caches, editor panel cpp
Cerrado:
leído: clear editor line paint caches, editor panel cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M11
leído: scan shell output for linker errors, request binary symbols panel, parse linker undefined reference, diagnostics for translation unit, diagnostic severity tr
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación genéricos de la consola y los marca en el editor; lo leído es un sistema específico para errores de enlazador linker que abre un panel de símbolos binarios, y un sistema LSP que filtra diagnósticos ya existentes pero no los parsea de la consola
Abierto:
- console parser
- shell output to diagnostics
- parse compiler error

### Trabajo 3
consulta: dónde se parsean los errores de compilación del texto de la consola para crear diagnósticos
keep: M7
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para crear diagnósticos
ESA pregunta no se relanza. Si el ancla era esa pregunta, cierra el claim citando lo leído. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: clear editor line paint caches, editor panel cpp

T2
    visto: scan shell output for linker errors, request binary symbols panel, parse linker undefined reference, diagnostics for translation unit
    extra: tick binary symbols panel, has value

T3
    visto: (nada)

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T2 → tick binary symbols panel
  T2 → has value
