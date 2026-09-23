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

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se reinicia el servidor de lenguaje LSP cuando se cae o deja de responder
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
encontré el mecanismo de reinicio: el cliente LSP detecta la caída en transport reader eof pero no reinicia directamente; el reinicio se gestiona en la aplicación mediante schedule debounced lsp restart que marca pending lsp restart true y restart lsp for workspace que ejecuta el reinicio real llamando a symbol provider workspace opened . El disparo ocurre desde apply workspace settings o setup build environment watching cambios de entorno config, y también desde lsp missing install . No hay un camino directo de transport reader eof a restart lsp for workspace en el código leído, lo que sugiere que la detección de caída en el cliente podría no estar conectada directamente al reinicio automático en la aplicación, o que falta un esl

T2 preguntó:
dónde se maneja el evento transport reader eof para conectar la caída del cliente LSP al reinicio automático
Cerrado de T2:
leído: on transport reader eof, start, stop, spawn language server

T3 preguntó:
dónde se invoca schedule debounced lsp restart al detectar que el servidor LSP se ha caído o no responde
Cerrado de T3:
leído: schedule debounced lsp restart, on transport reader eof, restart lsp for workspace, run, tick shutdown

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
