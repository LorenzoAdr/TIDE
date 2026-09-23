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
leído: wait until healthy, status text, health ok, restart lsp for workspace, schedule debounced lsp restart
Cerrado:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del servidor de lenguaje LSP para disparar reinicio; lo leído es la lógica de salud del backend L2 llama backend y el reinicio manual debounced del LSP clangd tras cambios de configuración, pero no hay un watchdog que detecte caída del LSP y dispare restart lsp for workspace automáticamente
Abierto:
- start server
- on connection lost
- on error

### Trabajo 2
consulta: dónde se maneja la desconexión o el error del servidor de lenguaje para intentar reconectar
keep: M9
leído: schedule debounced lsp restart
Cerrado:
leído: schedule debounced lsp restart
Abierto:
(nada; no copiar ids del pack)

### Trabajo 3
consulta: dónde se inicia el servidor de lenguaje y se establece la conexión inicial
keep: M7
leído: spawn language server, start, initialize
Cerrado:
encontré dónde se inicia el servidor y se establece la conexión: start llama a spawn language server que hace fork exec y devuelve transport start para el transporte, luego llama a initialize que envía la request initialize LSP y la notificación initialized
Abierto:
(nada; no copiar ids del pack)


# control_opened_v1
n=3  (visto+hops extra+circuito; sin código)

T1
    visto: wait until healthy, status text, health ok, restart lsp for workspace
    extra: ensure ready, start server

T2
    visto: schedule debounced lsp restart
    extra: apply workspace settings, setup build environment watching

T3
    visto: spawn language server, start, initialize
    extra: c str, set notification handler

entre abiertas:
  T1=>T2  mismo objeto: schedule debounced lsp restart
  T1=>T3  sin camino
  T2=>T3  sin camino
hacia el resto:
  T1 → ensure ready
  T1 → start server
  T2 → apply workspace settings
  T2 → setup build environment watching
  T3 → c str
  T3 → set notification handler

# examen
Ancla:
necesito que si el proceso del servidor de lenguaje LSP se cae o deja de responder, la aplicación lo detecte automáticamente y lo reinicie sin que el usuario tenga que hacer nada

T1 preguntó:
dónde se detecta que el servidor de lenguaje se ha caído o no responde para disparar el reinicio
Cerrado de T1:
ESA pregunta no se relanza. Lo leído no era el disparo. PROHIBIDO el mismo claim. do=cerrar ese mapeo. Si el ancla pedía más, un objeto distinto del pack; no el mismo claim en otro keep; no refutes el claim entero.
no encontré la detección de caída no-respuesta del servidor de lenguaje LSP para disparar reinicio; lo leído es la lógica de salud del backend L2 llama backend y el reinicio manual debounced del LSP clangd tras cambios de configuración, pero no hay un watchdog que detecte caída del LSP y dispare restart lsp for workspace automáticamente

T2 preguntó:
dónde se maneja la desconexión o el error del servidor de lenguaje para intentar reconectar
Cerrado de T2:
leído: schedule debounced lsp restart

T3 preguntó:
dónde se inicia el servidor de lenguaje y se establece la conexión inicial
Cerrado de T3:
encontré dónde se inicia el servidor y se establece la conexión: start llama a spawn language server que hace fork exec y devuelve transport start para el transporte, luego llama a initialize que envía la request initialize LSP y la notificación initialized

La última caza cerró. El siguiente tiro es un port del pack (entre abiertas / extra), no el ancla ni el mismo keep. Un Cerrado que afirma SU consulta no sella el ancla. Si entre abiertas dice sin camino, no pegues esos Cerrados como un solo mecanismo.

Legal ahora: cerrar
exploradores agotados. Cierra citando el mecanismo leído o la refutación. PROHIBIDO una coletilla ('el ancla queda contestada'). PROHIBIDO copiar ids.
JSON ahora. Primer carácter `{`.
