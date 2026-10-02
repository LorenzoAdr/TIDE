### Trabajo 1
consulta: dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
keep: M1
leído: schedule debounced lsp restart, restart lsp for workspace, run, on transport reader eof, stop, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de reinicio: el cliente LSP detecta la caída en transport reader eof pero no reinicia directamente; el reinicio se gestiona en la aplicación mediante schedule debounced lsp restart que marca pending lsp restart true y restart lsp for workspace que ejecuta el reinicio real llamando a symbol provider workspace opened . El disparo ocurre desde apply workspace settings o setup build environment watching cambios de entorno config, y también desde lsp missing install . No hay un camino directo de transport reader eof a restart lsp for workspace en el código leído, lo que sugiere que la detección de caída en el cliente podría no estar conectada directamente al reinicio automático en la aplicación, o que falta un esl
Abierto:
- pending lsp restart

### Trabajo 2
consulta: dónde se maneja el evento transport reader eof para conectar la caída del cliente LSP al reinicio automático
keep: M1 M2
leído: on transport reader eof, start, stop, spawn language server
Cerrado:
leído: on transport reader eof, start, stop, spawn language server
Abierto:
- stop lock

### Trabajo 3
consulta: dónde se invoca schedule debounced lsp restart al detectar que el servidor LSP se ha caído o no responde
keep: M1 M4
leído: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, run, tick shutdown
Cerrado:
leído: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, run, tick shutdown
Abierto:
- begin shutdown
- ensure backend started


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: schedule debounced lsp restart, restart lsp for workspace, run, on transport reader eof
    extra: apply workspace settings, setup build environment watching

T2
    visto: on transport reader eof, start, stop, spawn language server
    extra: initialize, request start

T3
    visto: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, run
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → apply workspace settings
  T1 → setup build environment watching
  T2 → initialize
  T2 → request start
  T3 → apply workspace settings
  T3 → setup build environment watching
