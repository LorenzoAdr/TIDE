### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M11
leído: on lsp notification, spawn language server, set notification handler, start, on transport reader eof, set reader eof handler, stop
Cerrado:
leído: on lsp notification, spawn language server, set notification handler, start, on transport reader eof, set reader eof handler, stop
Abierto:
(nada; no copiar ids del pack)

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP automáticamente tras detectar que se ha caído
keep: M3 M8
leído: schedule debounced lsp restart, restart lsp for workspace
Cerrado:
leído: schedule debounced lsp restart, restart lsp for workspace
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: hay camino detectar que el servidor de lenguaje LSP se ha caído y reiniciarlo automáticamente
keep: M10 M11
leído: spawn language server, start, on transport reader eof, stop, set background paused
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré camino de reinicio automático; lo leído muestra que on transport reader eof solo marca ready false y espera al proceso, sin llamar a start ni a ningún mecanismo de respawn
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on lsp notification, spawn language server, set notification handler, start
    extra: contains, is lsp trackable path

T2
    visto: schedule debounced lsp restart, restart lsp for workspace
    extra: apply workspace settings, setup build environment watching

T3
    visto: spawn language server, start, on transport reader eof, stop
    extra: c str, initialize

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  mismo objeto: spawn language server
  T2=>T3  sin camino
hacia el resto:
  T1 → contains
  T1 → is lsp trackable path
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → c str
  T3 → initialize
