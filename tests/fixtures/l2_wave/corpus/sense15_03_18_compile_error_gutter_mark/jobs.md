### Trabajo 1
consulta: dónde se detectan los errores de compilación en la salida de la consola
keep: M3 M12
leído: count workspace diagnostics, std
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de errores de compilación en la salida de la consola; lo leído es el conteo de diagnósticos LSP y utilidades de texto de la consola pero no el parser que convierte la salida del compilador en errores
Abierto:
- parse compiler output
- parse pty filter tokens
- count errors

### Trabajo 2
consulta: dónde se escanea la salida del compilador para extraer errores
keep: M11 M4
leído: build rows, format problem line
Cerrado:
leído: build rows, format problem line
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se dibuja la línea roja en el margen izquierdo del editor para marcar errores
keep: M8 M2
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Cerrado:
leído: rebuild diagnostics by line if changed, rebuild diagnostics by line
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: count workspace diagnostics, std
    extra: Make Main Layout, count errors

T2
    visto: build rows, format problem line
    extra: allows lsp ui, buffer text

T3
    visto: rebuild diagnostics by line if changed, rebuild diagnostics by line
    extra: apply lsp diagnostics to panel, push active file diagnostics from cache

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → Make Main Layout
  T1 → count errors
  T2 → allows lsp ui
  T2 → buffer text
  T3 → apply lsp diagnostics to panel
  T3 → push active file diagnostics from cache
