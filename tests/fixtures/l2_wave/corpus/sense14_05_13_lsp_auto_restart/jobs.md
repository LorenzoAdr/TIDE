### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje LSP se ha caído o dejado de responder
keep: M2 M6
leído: on transport reader eof, start, stop, set reader eof handler, reader thread
Cerrado:
encontré el mecanismo de detección: el servidor se considera caído cuando el pipe de salida cierra EOF . Esto ocurre en reader thread función wait fd readable detecta POLLHUP POLLERR o retorno 0, que invoca el reader eof handler establecido en start el cual llama a transport reader eof . Esta función marca ready false y limpia el PID, completando la detección.
Abierto:
- reader thread cuerpo completo

### Trabajo 2
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha caído
keep: M2 M7
leído: on transport reader eof, spawn language server, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático tras la caída; lo leído muestra que transport reader eof solo limpia el estado ready false waitpid y no invoca a start ni a spawn language server . El reinicio no ocurre en el cliente tras la detección de EOF.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se programa el reinicio diferido del servidor de lenguaje LSP tras un fallo
keep: M2 M5
leído: schedule debounced lsp restart, restart lsp for workspace, run, process build environment updates
Cerrado:
encontré el mecanismo de programación del reinicio diferido: schedule debounced lsp restart programa el estado pending lsp restart, deadline y process build environment updates lo evalúa y dispara restart lsp for workspace
Abierto:
- run custom event drain
- pending lsp restart


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: on transport reader eof, start, stop, set reader eof handler
    extra: set notification handler, set response acceptance filter

T2
    visto: on transport reader eof, spawn language server, start
    extra: c str, set notification handler

T3
    visto: schedule debounced lsp restart, restart lsp for workspace, run, process build environment updates
    extra: apply workspace settings, setup build environment watching

entre abiertas:
  T1=>T2  mismo objeto: on transport reader eof
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → set notification handler
  T1 → set response acceptance filter
  T2 → c str
  T2 → set notification handler
  T3 → apply workspace settings
  T3 → setup build environment watching
