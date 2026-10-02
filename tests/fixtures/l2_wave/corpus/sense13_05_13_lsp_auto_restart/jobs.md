### Trabajo 1
consulta: dónde se detecta que el servidor LSP se ha caído o no responde para disparar un reinicio
keep: M3 M5
leído: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída que dispare el reinicio; lo leído muestra que transport reader eof solo limpia estado ready false waitpid sin llamar a restart lsp for workspace y que restart lsp for workspace es invocado por run inicio y lsp missing install ausencia, pero no hay un caller desde el cliente LSP tras un EOF.
Abierto:
- on transport reader eof caller chain to restart
- stop caller chain to restart
- timeout detection logic

### Trabajo 2
consulta: dónde se detecta que el servidor LSP deja de responder por timeout para disparar un reinicio
keep: M6
leído: completion wait timeout ms, wait response, wait fd readable, on transport reader eof, start
Cerrado:
leído: completion wait timeout ms, wait response, wait fd readable, on transport reader eof, start
Abierto:
- send request

### Trabajo 3
consulta: dónde se invoca schedule debounced lsp restart tras un fallo de conexión
keep: M1
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, restart lsp for workspace, schedule debounced lsp restart
    extra: set notification handler, set reader eof handler

T2
    visto: completion wait timeout ms, wait response, wait fd readable, on transport reader eof
    extra: completions at, start async worker

T3
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  mismo objeto: schedule debounced lsp restart
  T2=>T3  mismo objeto: schedule debounced lsp restart
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → completions at
  T2 → start async worker
  T3 → apply workspace settings
  T3 → setup build environment watching
