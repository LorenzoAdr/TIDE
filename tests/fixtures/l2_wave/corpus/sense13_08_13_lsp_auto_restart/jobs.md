### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o no responde para disparar el reinicio
keep: M2 M3
leído: on transport reader eof, start, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de no responde timeout heartbeat; lo leído solo cubre la caída por EOF on transport reader eof y el reinicio manual arranque restart lsp for workspace
Abierto:
- on lsp notification
- set reader eof handler

### Trabajo 2
consulta: dónde se detecta que el servidor LSP no responde timeout o heartbeat para disparar el reinicio
keep: M3 M7
leído: wait response, reader loop, schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: wait response, reader loop, schedule debounced lsp restart, restart lsp for workspace
Abierto:
- send request

### Trabajo 3
consulta: dónde se reinicia el servidor LSP automáticamente tras detectar que se ha caído
keep: M5
leído: start, stop, on transport reader eof, restart lsp for workspace
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección automática de caída timeout heartbeat ni el disparo de reinicio automático; lo leído solo cubre la parada por EOF transport reader eof que no reinicia, y el reinicio manual restart lsp for workspace
Abierto:
- auto restart


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, restart lsp for workspace
    extra: set notification handler, set reader eof handler

T2
    visto: wait response, reader loop, schedule debounced lsp restart, restart lsp for workspace
    extra: send request, contains

T3
    visto: start, stop, on transport reader eof, restart lsp for workspace
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  mismo objeto: restart lsp for workspace
  T1=>T2  initialize → send lsp request → wait response
  T1=>T3  mismo objeto: on transport reader eof
  T2=>T3  mismo objeto: restart lsp for workspace
  T2=>T3  initialize → send lsp request → wait response
hacia el resto:
  T1 → set notification handler
  T1 → set reader eof handler
  T2 → send request
  T2 → contains
  T3 → set notification handler
  T3 → set reader eof handler
