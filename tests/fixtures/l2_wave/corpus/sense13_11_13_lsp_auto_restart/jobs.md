### Trabajo 1
consulta: dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
keep: M7
leído: wait until healthy, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída del servidor de lenguaje LSP clangd; lo leído es la lógica de salud del backend LLM llama backend y el reinicio manual configuracional del LSP application cpp, pero no el watchdog o handler de desconexión del LSP
Abierto:
- start server
- handle disconnect
- on connection lost
- clangd watchdog

### Trabajo 2
consulta: dónde se maneja la desconexión del servidor de lenguaje LSP para detectar que se ha caído
keep: M3
leído: on transport reader eof, start, set reader eof handler, reader loop
Cerrado:
encontré el mecanismo de detección de caída del servidor LSP: el transporte reader loop detecta el cierre del pipe EOF y dispara el handler registrado on transport reader eof, que marca el cliente como no listo y limpia el PID del hijo.
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha desconectado
keep: M6
leído: on transport reader eof, start
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático tras desconexión; lo leído es que transport reader eof solo limpia estado ready false waitpid y start registra el handler, pero no hay llamada a reinicio en el cliente LSP ni en el transport
Abierto:
- restart lsp on disconnect


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, health ok, restart lsp for workspace, schedule debounced lsp restart
    extra: ensure ready, start server

T2
    visto: on transport reader eof, start, set reader eof handler, reader loop
    extra: set notification handler, set response acceptance filter

T3
    visto: on transport reader eof, start
    extra: set notification handler, set reader eof handler

entre abiertas:
  T1=>T2  sin camino
  T1=>T3  sin camino
  T2=>T3  mismo objeto: on transport reader eof
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → set notification handler
  T2 → set response acceptance filter
  T3 → set notification handler
  T3 → set response acceptance filter
