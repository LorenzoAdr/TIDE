### Trabajo 1
consulta: dónde se leen los errores de compilación de la consola para marcarlos en el editor
keep: M5 M4
leído: console panel cpp, editor panel cpp, diagnostics cpp, lsp client cpp
Cerrado:
leído: console panel cpp, editor panel cpp, diagnostics cpp, lsp client cpp
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se parsean los errores de compilación del texto de la consola para extraer la línea y el mensaje
keep: (ninguno)
leído: (nada)
Cerrado:
El hijo cerró esta pregunta: no hay objeto. Está contestada. No se relanza.
Preguntó: dónde se parsean los errores de compilación del texto de la consola para extraer la línea y el mensaje
ESA pregunta no se relanza. Cierra ESE mapeo, o un objeto distinto del pack (un extra del Abierto); no el mismo claim en otro keep; no refutes el claim entero.
(vacío)
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se escanea la salida del shell para errores de compilación y se marca el margen del editor
keep: M2 M5
leído: scan shell output for linker errors
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de escaneo de errores de compilación en la salida del shell ni su vinculación al margen del editor; lo leído scan shell output for linker errors es un escáner específico para errores de enlace linker que actualiza un panel de símbolos binarios, no los diagnósticos del editor.
Abierto:
- parse compile errors
- shell output to diagnostics
- apply shell diagnostics to editor


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: console panel cpp, editor panel cpp, diagnostics cpp, lsp client cpp

T2
    visto: (nada)

T3
    visto: scan shell output for linker errors
    extra: tick binary symbols panel, has value

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T3 → tick binary symbols panel
  T3 → has value
