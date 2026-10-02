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
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído scan shell output for linker errors escanea la salida del shell únicamente para referencias indefinidas del enlazador y solicita símbolos binarios, no extrae diagnósticos de compilación ni los aplica al editor
Abierto:
- parse compilation errors
- apply diagnostics to editor
- console output to diagnostics

### Trabajo 3
consulta: dónde se extraen los errores de compilación de la salida de la consola
keep: M11
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo que lee errores de compilación de la consola para marcarlos en el editor; lo leído scan shell output for linker errors escanea la salida del shell únicamente para referencias indefinidas del linker parse linker undefined reference y las envía al panel de símbolos binarios, no al editor como errores de compilación
Abierto:
- parse linker undefined reference
- request binary symbols panel
- tick binary symbols panel


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: (nada)

T2
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

T3
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: scan shell output for linker errors
hacia el resto:
  T2 → tick binary symbols panel
  T2 → has value
  T3 → tick binary symbols panel
  T3 → has value
