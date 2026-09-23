Consulta del usuario (ancla, claim; no la copies entera a un hijo):
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

Atlas del explorador (peek/nucleus/port de todo el mazo. Eligen barrio; la consulta nombra el fenómeno, no el símbolo):
n=12  (owns=objeto; same=M* clon; peek objeto+verbo; 1–2 ids)
M1  kind=other  ov=3  stems: lsp missing prompt
owns: lsp missing prompt
gap: state,effect
peek: lsp missing prompt catalog, lsp missing prompt lsp missing prompt for status key
peek-edge: lsp missing prompt lsp missing prompt for status key -call-> lsp missing prompt catalog
M2  kind=other  ov=3  stems: lsp symbol provider
owns: lsp symbol provider
gap: state,trigger,effect
peek: lsp symbol provider configure bash language server
M3  kind=caller  ov=0  stems: model store
owns: model store
gap: state,effect
peek: model store has llama server, model store resolve llama server
port: M3=>M6 llama backend ensure completion server -call-> model store resolve llama server
M4  same=M2  kind=other  ov=3  stems: lsp symbol provider
peek: lsp symbol provider join fortran startup thread
M5  kind=latch  ov=3  stems: application application
owns: application
nucleus: pending lsp restart , last lsp environment fingerprint 
peek: application restart lsp for workspace, application schedule debounced lsp restart
port: M5=>M7 application restart lsp for workspace -write-> status message
M6  kind=caller  ov=0  stems: embedding backend llama backend embedding backend llama backend
owns: embedding backend
nucleus: server pid , errno, server stamp , server pid 
peek: embedding backend start server, llama backend start completion server
port: M6=>M3 llama backend ensure completion server -call-> model store resolve llama server
M7  same=M5  kind=caller  ov=3  stems: application
peek: application apply event, application restart lsp for workspace
M8  kind=caller  ov=0  stems: ai controller ai controller
owns: ai controller
nucleus: settings 
peek: ai controller begin insert at, llama net apply ai runtime env
port: M8=>M7 application run -call-> ai controller begin insert at
M9  kind=hole  ov=0  stems: settings  workspace config app settings model store
owns: settings  / llama net apply ai runtime env
nucleus: settings 
peek: llama net apply ai runtime env, api base
peek-edge: llama net apply ai runtime env -write-> api base
M10  kind=hole  ov=0  stems: l2 effect registry l2 effect registry
owns: l2 effect registry
gap: no state
peek: l2 effect registry list file fns, l2 effect registry read abs
peek-edge: l2 effect registry list file fns -call-> l2 effect registry read abs
M11  same=M8  kind=hole  ov=0  stems: ai controller
peek: l2 effect registry ascii lower copy, l2 effect registry collect text tokens
M12  kind=hole  ov=0  stems: l2 brain remote l2 brain remote
owns: l2 brain remote
gap: no state
peek: l2 brain remote make l2 brain
bridges: T4[M3,M6]
holes: llama net apply ai runtime env l2 effect registry list file fns l2 effect registry ascii lower copy l2 brain remote make l2 brain

Trabajos ya hechos:
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

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de detección de caída del servidor de lenguaje LSP clangd; lo leído es la lógica de salud del backend LLM llama backend y el reinicio manual configuracional del LSP application cpp, pero no el watchdog o handler de desconexión del LSP

T2 preguntó:
dónde se maneja la desconexión del servidor de lenguaje LSP para detectar que se ha caído
Cerrado de T2:
encontré el mecanismo de detección de caída del servidor LSP: el transporte reader loop detecta el cierre del pipe EOF y dispara el handler registrado on transport reader eof, que marca el cliente como no listo y limpia el PID del hijo.

T3 preguntó:
dónde se reinicia el servidor de lenguaje LSP tras detectar que se ha desconectado
Cerrado de T3:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré el mecanismo de reinicio automático tras desconexión; lo leído es que transport reader eof solo limpia estado ready false waitpid y start registra el handler, pero no hay llamada a reinicio en el cliente LSP ni en el transport

ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.

Legal ahora: cerrar
exploradores agotados. Lo leído no era el disparo. Cierra ese mapeo; no cites esos nombres como el mecanismo del ancla. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
